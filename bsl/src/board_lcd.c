#include "board_lcd.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_gpio.h"

/*
 * 寄存器值出处（[参考工程] Drivers/BSP/LCD/ltdc.c 的 ltdc_init / ltdc_clk_set）：
 *   - PLLSAICFGR = (N << 6) | (R << 28)，DCKCFGR[17:16] = DIVR 编码
 *   - GCR 极性：像素时钟不反向、DE/VS/HS 均低电平有效
 *   - SSCR/BPCR/AWCR/TWCR 的累加口径（脉宽、后沿、有效区、总区）
 *   - 层 1：PFCR/CACR/DCCR/BFCR/CFBAR/CFBLR/CFBLNR
 * 数值本身（800×480、48/88/40、3/32/13、N=396/R=3/DIVR=/4）来自 DTB，
 * 不来自这份代码。
 *
 * PLLSAI 输入是 HSE/PLLM = 25/25 = 1 MHz，与主 PLL 的 N 无关：所以参考工程在
 * 192 MHz 下的 (396, 3, /4) 在本工程 168 MHz 下同样得到 396/3/4 = 33 MHz 像素
 * 时钟。这不是"抄参数碰巧对"，而是 PLLSAI 独立于 SYSCLK。
 */

#define LCD_PLL_RETRY 0x1FFFU
#define LCD_BFCR_CONST_ALPHA ((6UL << 8) | 7UL) /* 恒定 Alpha 混合，与参考工程一致 */

static int s_ready;
static void *s_fb;
static uint32_t s_width;
static uint32_t s_height;
static uint32_t s_hsw;
static uint32_t s_hbp;
static uint32_t s_vsw;
static uint32_t s_vbp;
static uint32_t s_pixfmt;
static GPIO_TypeDef *s_bl_port;
static unsigned s_bl_pin;
static int s_bl_active_high;

/* PLLSAI 是像素时钟的唯一来源。关掉再配：运行中改 PLLSAICFGR 而不先关电源，
 * 锁定位会一直是"已锁"，配置写进去看不出生效过。 */
static bsl_err_t lcd_clock_config(const board_rt_panel_t *p)
{
    uint32_t retry = 0U;

    RCC->CR &= ~RCC_CR_PLLSAION;
    while ((RCC->CR & RCC_CR_PLLSAIRDY) != 0U && retry < LCD_PLL_RETRY) {
        retry++;
    }
    if (retry >= LCD_PLL_RETRY) {
        return BSL_ERR_TIMEOUT;
    }

    RCC->PLLSAICFGR = ((p->pllsai_n & 0x1FFU) << 6) | ((p->pllsai_r & 0x7U) << 28);
    RCC->DCKCFGR &= ~RCC_DCKCFGR_PLLSAIDIVR;
    RCC->DCKCFGR |= (p->pllsai_divr & 0x3U) << 16U;

    retry = 0U;
    RCC->CR |= RCC_CR_PLLSAION;
    while ((RCC->CR & RCC_CR_PLLSAIRDY) == 0U && retry < LCD_PLL_RETRY) {
        retry++;
    }
    if (retry >= LCD_PLL_RETRY) {
        return BSL_ERR_TIMEOUT;
    }
    return BSL_OK;
}

/* 时序四项的期望值，selftest 与 init 共用同一份公式：两处各写一遍，
 * 改了一处而另一处没改时，自检会"按旧的错值判定通过"。 */
static uint32_t lcd_exp_sscr(void)
{
    return (s_vsw - 1U) | ((s_hsw - 1U) << 16);
}

static uint32_t lcd_exp_bpcr(void)
{
    return (s_vsw + s_vbp - 1U) | ((s_hsw + s_hbp - 1U) << 16);
}

static uint32_t lcd_exp_awcr(void)
{
    return (s_vsw + s_vbp + s_height - 1U)
           | ((s_hsw + s_hbp + s_width - 1U) << 16);
}

static uint32_t lcd_exp_twcr(void)
{
    board_rt_panel_t p;

    /* 前廊只在 TWCR 里出现，所以这里重新取一次 DTB（不额外缓存 s_hfp/s_vfp：
     * 多一个副本就多一处可能与 DTB 漂移的地方）。init 在调用前已确认 DTB 可取。 */
    if (board_rt_panel(&p) == 0) {
        return 0U;
    }
    return (s_vsw + s_vbp + s_height + p.vfp - 1U)
           | ((s_hsw + s_hbp + s_width + p.hfp - 1U) << 16);
}

bsl_err_t board_lcd_init(void)
{
    board_rt_panel_t panel;
    GPIO_TypeDef *bl_port = NULL;
    unsigned bl_pin = 0U;
    int bl_active_high = 0;
    unsigned groups;
    unsigned i;

    if (board_rt_panel(&panel) == 0) {
        return BSL_ERR_INVAL;
    }
    if (panel.framebuffer == 0U) {
        return BSL_ERR_INVAL;
    }
    /* 帧缓冲的落脚点必须是已初始化的 SDRAM：先写画面再初始化 SDRAM 的话，
     * 写入会被丢弃，屏上是雪花而不是黑屏，看起来像"面板坏了"。 */
    if (bsl_device_is_ready(BSL_DEV_SDRAM) == 0) {
        return BSL_ERR_INVAL;
    }
    if (board_rt_panel_backlight(&bl_port, &bl_pin, &bl_active_high) == 0) {
        return BSL_ERR_INVAL;
    }
    groups = board_rt_panel_pin_groups();
    if (groups == 0U) {
        return BSL_ERR_INVAL;
    }

    if (lcd_clock_config(&panel) != BSL_OK) {
        return BSL_ERR_TIMEOUT;
    }

    /* 全组成功才继续：任一端口配不上，数据线会少一位，画面是"整片偏色"，
     * 而没有任何读数指出是哪个端口漏了。 */
    for (i = 0U; i < groups; i++) {
        if (bsl_gpio_init_af(board_rt_panel_pin_port(i),
                             board_rt_panel_pin_mask(i),
                             board_rt_panel_pin_af(i),
                             GPIO_PULLUP,
                             GPIO_SPEED_FREQ_VERY_HIGH) == 0) {
            return BSL_ERR_IO;
        }
    }

    /* 背光先熄：面板上电瞬间的随机内容不该被点亮给人看见。 */
    s_bl_port = bl_port;
    s_bl_pin = bl_pin;
    s_bl_active_high = bl_active_high;
    if (bsl_gpio_init_output_pp(bl_port, (uint16_t)(1U << bl_pin),
                               GPIO_NOPULL, GPIO_SPEED_FREQ_LOW) == 0) {
        return BSL_ERR_IO;
    }
    board_lcd_backlight(0);

    RCC->APB2ENR |= RCC_APB2ENR_LTDCEN;
    (void)RCC->APB2ENR; /* 读回一次，保证时钟已生效再写 LTDC 寄存器 */

    s_width = panel.width;
    s_height = panel.height;
    s_hsw = panel.hsw;
    s_hbp = panel.hbp;
    s_vsw = panel.vsw;
    s_vbp = panel.vbp;
    s_pixfmt = panel.pixel_format;
    s_fb = (void *)panel.framebuffer;

    /* GCR 全 0：像素时钟不反向，DE/VS/HS 都是低电平有效（参考工程除 10.1 寸外同此）。 */
    LTDC->GCR = 0U;
    LTDC->SSCR = lcd_exp_sscr();
    LTDC->BPCR = lcd_exp_bpcr();
    LTDC->AWCR = lcd_exp_awcr();
    LTDC->TWCR = lcd_exp_twcr();
    LTDC->BCCR = 0x000000U; /* 背景层：黑 */

    /* 层 1 = 唯一一层，直接铺满面板。 */
    LTDC_Layer1->PFCR = s_pixfmt;
    LTDC_Layer1->CACR = 255U;                       /* 层恒定 Alpha：不透明 */
    LTDC_Layer1->DCCR = 0U;                         /* 默认颜色：黑、全透明 */
    LTDC_Layer1->BFCR = LCD_BFCR_CONST_ALPHA;
    LTDC_Layer1->CFBAR = (uint32_t)panel.framebuffer;
    LTDC_Layer1->CFBLR = ((s_width * 2U) << 16) | (s_width * 2U + 3U);
    LTDC_Layer1->CFBLNR = s_height;
    /* 窗口以面板坐标为基准：起始 = 后沿+脉宽，结束 = 起始+宽-1（参考工程同口径）。 */
    LTDC_Layer1->WHPCR = (s_hsw + s_hbp)
                         | ((s_hsw + s_hbp + s_width - 1U) << 16);
    LTDC_Layer1->WVPCR = (s_vsw + s_vbp)
                         | ((s_vsw + s_vbp + s_height - 1U) << 16);
    LTDC_Layer1->CR = 1U; /* 层使能 */
    LTDC->SRCR = 1U;      /* 立即重载 */

    LTDC->GCR |= 1U; /* 打开 LTDC */

    /* 清成黑再点背光：上电时 SDRAM 里是 POST 写过的模式字，不清屏直接点亮
     * 会看到一片花点，容易被当成"面板初始化失败"。 */
    board_lcd_fill(0U, 0U, s_width, s_height, 0x0000U);

    s_ready = 1;
    board_lcd_backlight(1);

    /* 等首批帧出完再返回，让紧接着的 POST 打点落在"屏已稳定"之后。
     * 33 MHz 像素时钟下一帧 800×480 约 13 ms，20 ms 够一帧半。 */
    HAL_Delay(20U);
    return BSL_OK;
}

void *board_lcd_base(void)
{
    return (s_ready != 0) ? s_fb : NULL;
}

uint32_t board_lcd_width(void)
{
    return (s_ready != 0) ? s_width : 0U;
}

uint32_t board_lcd_height(void)
{
    return (s_ready != 0) ? s_height : 0U;
}

void board_lcd_backlight(int on)
{
    GPIO_PinState level;

    if (s_bl_port == NULL) {
        return;
    }
    level = ((on != 0) == (s_bl_active_high != 0)) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    bsl_gpio_write(s_bl_port, (uint16_t)(1U << s_bl_pin), level);
}

void board_lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t rgb565)
{
    uint32_t row;
    uint32_t col;
    volatile uint16_t *fb;

    if (s_ready == 0) {
        return;
    }
    if (x >= s_width || y >= s_height) {
        return;
    }
    if (w > s_width - x) {
        w = s_width - x;
    }
    if (h > s_height - y) {
        h = s_height - y;
    }
    fb = (volatile uint16_t *)s_fb;
    for (row = 0U; row < h; row++) {
        for (col = 0U; col < w; col++) {
            fb[(y + row) * s_width + x + col] = rgb565;
        }
    }
}

unsigned board_lcd_selftest(void)
{
    unsigned bad = 0U;

    if (s_ready == 0) {
        return 0xFFFFFFFFU;
    }
    if (LTDC->SSCR != lcd_exp_sscr()) {
        bad++;
    }
    if (LTDC->BPCR != lcd_exp_bpcr()) {
        bad++;
    }
    if (LTDC->AWCR != lcd_exp_awcr()) {
        bad++;
    }
    if (LTDC->TWCR != lcd_exp_twcr()) {
        bad++;
    }
    if (LTDC_Layer1->CFBAR != (uint32_t)s_fb) {
        bad++;
    }
    if (LTDC_Layer1->CFBLNR != s_height) {
        bad++;
    }
    if (LTDC_Layer1->PFCR != s_pixfmt) {
        bad++;
    }
    return bad;
}

const bsl_panel_api_t bsl_api_brd_rgb_panel = {
    .framebuffer = board_lcd_base,
    .width = board_lcd_width,
    .height = board_lcd_height,
};

const bsl_device_driver_t bsl_driver_brd_rgb_panel = {
    .init = board_lcd_init,
};
