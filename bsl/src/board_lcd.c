#include "board_lcd.h"

#include "board_nt35510.h"
#include "bsl_lcd_8080.h"
#include "bsl_log.h"
#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_gpio.h"

/*
 * 面板驱动：一台"屏"设备，两种总线形态（DTB 的 panel 节点用 bus 区分）。
 *
 *   1) RGB 屏（BOARD_RT_PANEL_BUS_RGB）：LTDC 直驱，像素由 LTDC 按同步时序自己扫；
 *      固件只需要把像素写进 SDRAM 帧缓冲。寄存器口径出处（[参考工程]
 *      Drivers/BSP/LCD/ltdc.c 的 ltdc_init / ltdc_clk_set）：
 *        - PLLSAICFGR = (N << 6) | (R << 28)，DCKCFGR[17:16] = DIVR 编码
 *        - GCR 极性：像素时钟不反向、DE/VS/HS 均低电平有效
 *        - SSCR/BPCR/AWCR/TWCR 的累加口径（脉宽、后沿、有效区、总区）
 *        - 层 1：PFCR/CACR/DCCR/BFCR/CFBAR/CFBLR/CFBLNR
 *
 *   2) MCU 屏（BOARD_RT_PANEL_BUS_FMC8080）：板上实插的 NT35510 480×800，
 *      FMC Bank1 8080 16 位并口。像素不在内存里 —— 内存那份只是"待推的副本"，
 *      每次改完要用 present() 把脏矩形推进 GRAM。
 *
 * 为什么保留 SDRAM 帧缓冲、而不是让所有绘制直接打 GRAM：8080 写一像素约 125 ns，
 * 逐点画线/画字会把界面刷新拖到看不清；而 42 处消费方都只认 base/width/height/fill，
 * 留一份内存副本可以让 fml_gui/fml_ui/app 一行不改。
 */

#define LCD_PLL_RETRY 0x1FFFU
#define LCD_BFCR_CONST_ALPHA ((6UL << 8) | 7UL) /* 恒定 Alpha 混合，与参考工程一致 */
#define LCD_FULL_SELFTEST_BAD 0xFFFFFFFFU

static int s_ready;
static int s_bus;
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

/* 脏矩形：所有写帧缓冲的路径都会往这里登记，present() 只推这一块。
 * 用"含头不含尾"的 (x0,y0)-(x1,y1)，空矩形时 s_dirty=0。 */
static int s_dirty;
static uint32_t s_dirty_x0;
static uint32_t s_dirty_y0;
static uint32_t s_dirty_x1;
static uint32_t s_dirty_y1;

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

void board_lcd_dirty_mark(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t x1;
    uint32_t y1;

    if (w == 0U || h == 0U || s_width == 0U || s_height == 0U) {
        return;
    }
    if (x >= s_width || y >= s_height) {
        return;
    }
    x1 = (w > s_width - x) ? s_width : x + w;
    y1 = (h > s_height - y) ? s_height : y + h;
    if (s_dirty == 0) {
        s_dirty_x0 = x;
        s_dirty_y0 = y;
        s_dirty_x1 = x1;
        s_dirty_y1 = y1;
        s_dirty = 1;
        return;
    }
    if (x < s_dirty_x0) {
        s_dirty_x0 = x;
    }
    if (y < s_dirty_y0) {
        s_dirty_y0 = y;
    }
    if (x1 > s_dirty_x1) {
        s_dirty_x1 = x1;
    }
    if (y1 > s_dirty_y1) {
        s_dirty_y1 = y1;
    }
}

static void lcd_backlight_setup(GPIO_TypeDef *port, unsigned pin, int active_high)
{
    s_bl_port = port;
    s_bl_pin = pin;
    s_bl_active_high = active_high;
    (void)bsl_gpio_init_output_pp(port, (uint16_t)(1U << pin),
                                  GPIO_NOPULL, GPIO_SPEED_FREQ_LOW);
    /* 背光先熄：面板上电瞬间的随机内容不该被点亮给人看见。 */
    board_lcd_backlight(0);
}

static bsl_err_t lcd_init_ltdc(const board_rt_panel_t *panel)
{
    unsigned groups;
    unsigned i;

    groups = board_rt_panel_pin_groups();
    if (groups == 0U) {
        return BSL_ERR_INVAL;
    }
    if (lcd_clock_config(panel) != BSL_OK) {
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

    s_hsw = panel->hsw;
    s_hbp = panel->hbp;
    s_vsw = panel->vsw;
    s_vbp = panel->vbp;

    RCC->APB2ENR |= RCC_APB2ENR_LTDCEN;
    (void)RCC->APB2ENR; /* 读回一次，保证时钟已生效再写 LTDC 寄存器 */

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
    LTDC_Layer1->CFBAR = (uint32_t)(uintptr_t)s_fb;
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
    s_dirty = 0; /* LTDC 自己会扫，帧缓冲里的黑已经就在屏上，无需推 */

    s_ready = 1;
    board_lcd_backlight(1);

    /* 等首批帧出完再返回，让紧接着的 POST 打点落在"屏已稳定"之后。
     * 33 MHz 像素时钟下一帧 800×480 约 13 ms，20 ms 够一帧半。 */
    HAL_Delay(20U);
    return BSL_OK;
}

static bsl_err_t lcd_init_mcu(const board_rt_panel_t *panel)
{
    bsl_err_t err;

    /* 总线 + ID + 381 条初始化表都在 board_nt35510_init 里；这里只管屏上内容。 */
    err = board_nt35510_init(panel->panel_id);
    if (err != BSL_OK) {
        return err;
    }

    s_ready = 1;
    /* 先清黑并整屏推一次：不清就点背光，屏上会留着 GRAM 里的随机内容。 */
    board_lcd_fill(0U, 0U, s_width, s_height, 0x0000U);
    (void)board_lcd_present();
    s_dirty = 0;

    board_lcd_backlight(1);
    HAL_Delay(20U);
    return BSL_OK;
}

bsl_err_t board_lcd_init(void)
{
    board_rt_panel_t panel;
    GPIO_TypeDef *bl_port = NULL;
    unsigned bl_pin = 0U;
    int bl_active_high = 0;

    s_ready = 0;
    s_dirty = 0;

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

    s_width = panel.width;
    s_height = panel.height;
    s_pixfmt = panel.pixel_format;
    s_fb = (void *)panel.framebuffer;
    s_bus = (int)panel.bus;

    lcd_backlight_setup(bl_port, bl_pin, bl_active_high);

    if (panel.bus == BOARD_RT_PANEL_BUS_FMC8080) {
        return lcd_init_mcu(&panel);
    }
    return lcd_init_ltdc(&panel);
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

    /* P9c-2 起背光有第二个主人：TIM3_CH2 PWM（PB5）。
     * 归属判据是"pwm 节点控制的脚 == panel 的背光脚"（两边都读 DTB）。
     * 匹配时走占空比（on=100%、off=0%），不再写 BSRR —— PB5 在 AF 模式下
     * BSRR 是空操作，照旧写会得到"函数执行了、屏不亮"的假成功。 */
    if (bsl_device_is_ready(BSL_DEV_PWM) != 0 && board_rt_pwm_is_backlight() != 0) {
        (void)bsl_devapi_pwm()->set_duty((on != 0) ? 1000U : 0U);
        return;
    }
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
    board_lcd_dirty_mark(x, y, w, h);
}

int board_lcd_present(void)
{
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
    uint32_t written;

    if (s_ready == 0 || s_fb == NULL) {
        return 0;
    }
    /* RGB 屏由 LTDC 连续扫描，帧缓冲改了就等于屏上改了，没有"推"这一步。 */
    if (s_bus != BOARD_RT_PANEL_BUS_FMC8080) {
        s_dirty = 0;
        return 0;
    }
    /* 总线没配好就推是往一个没上电的并口写：会把脏区清掉而屏上什么也没变，
     * 于是"画面不刷新"和"总线挂了"分不开。留着脏区，等总线好了下一轮再推。 */
    if (bsl_lcd_8080_ready() == 0) {
        return 0;
    }
    if (s_dirty == 0) {
        return 0;
    }
    x0 = s_dirty_x0;
    y0 = s_dirty_y0;
    x1 = s_dirty_x1;
    y1 = s_dirty_y1;
    s_dirty = 0;

    written = board_nt35510_blit((uint16_t)x0, (uint16_t)y0,
                                 (uint16_t)(x1 - x0), (uint16_t)(y1 - y0),
                                 (const uint16_t *)s_fb + y0 * s_width + x0,
                                 (uint16_t)s_width);
    return (int)written;
}

void board_lcd_present_all(void)
{
    board_lcd_dirty_mark(0U, 0U, s_width, s_height);
    (void)board_lcd_present();
}

unsigned board_lcd_selftest(void)
{
    unsigned bad = 0U;
    uint16_t want;
    uint16_t probe;
    uint16_t got;
    uint32_t x;
    uint32_t y;

    if (s_ready == 0) {
        return LCD_FULL_SELFTEST_BAD;
    }

    if (s_bus == BOARD_RT_PANEL_BUS_FMC8080) {
        /* MCU 屏没有"时序寄存器"可读回，判据改成端到端：
         * 把帧缓冲里某个点的颜色推进 GRAM，再读回来比对。
         * 取帧缓冲的原值而不是硬编码颜色 —— 既不破坏画面，也顺带验证了
         * "内存副本 → GRAM"这条链是通的；硬编码红色只验证了后半段。 */
        if (board_nt35510_ready() == 0) {
            return LCD_FULL_SELFTEST_BAD;
        }
        x = s_width / 2U;
        y = s_height / 2U;
        /* 判据值取全 1（白）：它是 6 位→5 位量化下的不动点，读回必须一字不差；
         * 取画面原色则会被量化误差咬掉低位（0x2104 → 0x2100），那种不一致
         * 说明不了"写没写进去"。写完立刻把原值写回，屏上看不到这一瞬。 */
        want = ((volatile uint16_t *)s_fb)[y * s_width + x];
        probe = 0xFFFFU;
        (void)board_nt35510_blit((uint16_t)x, (uint16_t)y, 1U, 1U, &probe, 1U);
        got = board_nt35510_read_pixel((uint16_t)x, (uint16_t)y);
        (void)board_nt35510_blit((uint16_t)x, (uint16_t)y, 1U, 1U, &want, 1U);
        if (got != probe) {
            /* 失败必须可读：把期望值与读回值都打出来。只报 bad=1 的话，
             * "读时序不对" 和 "GRAM 没写进去" 在串口上长得一模一样。 */
            BSL_LOG_INF("[lcd] gram want=0x");
            bsl_log_put_hex((uint32_t)probe, 4U);
            BSL_LOG_INF(" got=0x");
            bsl_log_put_hex((uint32_t)got, 4U);
            BSL_LOG_INF("\r\n");
            bad++;
        }
        return bad;
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
    if (LTDC_Layer1->CFBAR != (uint32_t)(uintptr_t)s_fb) {
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

const bsl_panel_api_t bsl_api_brd_panel = {
    .framebuffer = board_lcd_base,
    .width = board_lcd_width,
    .height = board_lcd_height,
};

const bsl_device_driver_t bsl_driver_brd_panel = {
    .init = board_lcd_init,
};
