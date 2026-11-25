#include "board_rt.h"

#include <string.h>

#include "bsl_fdt.h"

#define RT_MAX_ITEMS 8U
#define RT_MAX_PIN_GROUPS 6U

#define RT_COMPAT_CLOCKS "brd,board-clocks"
#define RT_COMPAT_SDRAM "brd,fmc-sdram"
#define RT_COMPAT_PANEL "brd,rgb-panel"
#define RT_COMPAT_PANEL_MCU "novatek,nt35510"
#define RT_COMPAT_TOUCH "goodix,gt9147"
#define RT_COMPAT_NOR "winbond,w25q256"
#define RT_COMPAT_LEDS "brd,gpio-leds"
#define RT_COMPAT_KEYS "brd,gpio-keys"
#define RT_COMPAT_I2C "brd,i2c-gpio"
#define RT_COMPAT_BEEP "brd,pcf8574-beep"
#define RT_COMPAT_UART "brd,usart-ch340"
#define RT_COMPAT_RTC "brd,rtc"
#define RT_COMPAT_ADC "brd,adc-temp"
#define RT_COMPAT_SAI "brd,sai"

typedef struct {
    int parsed;
    uint32_t hse_hz;

    unsigned led_count;
    GPIO_TypeDef *led_port;
    uint16_t led_pin[RT_MAX_ITEMS];
    int led_active_low;

    unsigned key_count;
    GPIO_TypeDef *key_port[RT_MAX_ITEMS];
    uint16_t key_pin[RT_MAX_ITEMS];
    GPIO_PinState key_pressed[RT_MAX_ITEMS];

    int i2c_ok;
    GPIO_TypeDef *scl_port;
    uint16_t scl_pin;
    GPIO_TypeDef *sda_port;
    uint16_t sda_pin;

    int beep_via_pcf8574;
    uint8_t pcf8574_addr7;
    unsigned beep_bit;
    int beep_active_high;

    int uart_ok;
    uintptr_t uart_reg;
    uint32_t uart_baud;
    char tx_port;
    unsigned tx_pin;
    char rx_port;
    unsigned rx_pin;

    int sdram_ok;
    uintptr_t sdram_base;
    uint32_t sdram_size;
    uint32_t sdram_sdcr;
    uint32_t sdram_sdtr;
    uint32_t sdram_refresh;
    unsigned sdram_group_count;
    GPIO_TypeDef *sdram_group_port[RT_MAX_PIN_GROUPS];
    uint16_t sdram_group_mask[RT_MAX_PIN_GROUPS];
    uint32_t sdram_group_af[RT_MAX_PIN_GROUPS];

    int panel_ok;
    board_rt_panel_t panel;
    unsigned panel_group_count;
    GPIO_TypeDef *panel_group_port[RT_MAX_PIN_GROUPS];
    uint16_t panel_group_mask[RT_MAX_PIN_GROUPS];
    uint32_t panel_group_af[RT_MAX_PIN_GROUPS];
    GPIO_TypeDef *panel_bl_port;
    unsigned panel_bl_pin;
    int panel_bl_active_high;
    /* MCU 屏（FMC 8080）的四根控制脚：DTB 里各是一条 3-cell 属性。 */
    GPIO_TypeDef *panel_ctrl_port[BOARD_RT_LCD_PIN_COUNT];
    unsigned panel_ctrl_pin[BOARD_RT_LCD_PIN_COUNT];
    uint32_t panel_ctrl_af[BOARD_RT_LCD_PIN_COUNT];

    int touch_ok;
    board_rt_touch_t touch;

    int nor_ok;
    board_rt_nor_t nor;

    int rtc_ok;
    board_rt_rtc_t rtc;

    int adc_ok;
    board_rt_adc_t adc;

    int sai_ok;
    board_rt_sai_t sai;
    unsigned sai_group_count;
    GPIO_TypeDef *sai_group_port[RT_MAX_PIN_GROUPS];
    uint16_t sai_group_mask[RT_MAX_PIN_GROUPS];
    uint32_t sai_group_af[RT_MAX_PIN_GROUPS];

    int imu_ok;
    board_rt_mpu9250_t imu;

    int als_ok;
    board_rt_ap3216c_t als;

    int eeprom_ok;
    board_rt_eeprom_t eeprom;

    int tpad_ok;
    board_rt_tpad_t tpad;

    int can_ok;
    board_rt_can_t can;

    int usart_ok;
    board_rt_usart_t usart;
    int nand_ok;
    board_rt_nand_t nand;
    int dma_ok;
    board_rt_dma_t dma;
    int pwm_ok;
    board_rt_pwm_t pwm;
    int exti_ok;
    board_rt_exti_t exti;
    int dac_ok;
    board_rt_dac_t dac;
} board_rt_t;

static board_rt_t s_rt;

/* HAL 的 GPIOA..GPIOK 是编译期符号，DTB 的 reg 只给基址，按基址对齐。
 * 漏一个端口 → 该端口的设备静默不动作，所以这里必须覆盖 A..K。 */
static GPIO_TypeDef *port_of_base(uint32_t base)
{
    switch (base) {
    case GPIOA_BASE: return GPIOA;
    case GPIOB_BASE: return GPIOB;
    case GPIOC_BASE: return GPIOC;
    case GPIOD_BASE: return GPIOD;
    case GPIOE_BASE: return GPIOE;
    case GPIOF_BASE: return GPIOF;
    case GPIOG_BASE: return GPIOG;
    case GPIOH_BASE: return GPIOH;
    case GPIOI_BASE: return GPIOI;
    case GPIOJ_BASE: return GPIOJ;
    case GPIOK_BASE: return GPIOK;
    default: return NULL;
    }
}

GPIO_TypeDef *board_rt_port_of_phandle(uint32_t phandle)
{
    bsl_fdt_node_t ctrl = bsl_fdt_by_phandle(phandle);
    uint32_t reg[2];

    if (ctrl == NULL) {
        return NULL;
    }
    if (bsl_fdt_prop_cells(ctrl, "reg", reg, 2U) < 1U) {
        return NULL;
    }
    return port_of_base(reg[0]);
}

/* gpios = <&ctrl pin flags>：三 cell。返回 1 表示成功。 */
static int gpio_spec(bsl_fdt_node_t node, GPIO_TypeDef **port, unsigned *pin,
                     uint32_t *flags)
{
    uint32_t cells[3];

    if (node == NULL || bsl_fdt_prop_cells(node, "gpios", cells, 3U) != 3U) {
        return 0;
    }
    *port = board_rt_port_of_phandle(cells[0]);
    if (*port == NULL || cells[1] > 15U) {
        return 0;
    }
    *pin = (unsigned)cells[1];
    if (flags != NULL) {
        *flags = cells[2];
    }
    return 1;
}

static void parse_clocks(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_CLOCKS, 0U);

    if (node == NULL) {
        return;
    }
    /* 0 的语义是"DTB 未声明 HSE"，不是"某个频率"：消费端 board_system_init()
     * 只对非 0 值做一致性校验。早前这里 (void) 吞返回值，属性缺失或长度不对时
     * hse_hz 静默停在 0，谁都看不出 DTB 里根本没写这条 —— 于是"板级事实在 DTB"
     * 只是看起来成立。 */
    if (bsl_fdt_prop_u32(node, "hse-frequency", &s_rt.hse_hz) == 0) {
        s_rt.hse_hz = 0U;
    }
}

/*
 * leds：gpios 的第三个 cell 与组属性 active-low 是**同一事实的两份拷贝**
 * （生成器两边都写）。早前 C 只读属性、把 flags 丢掉：两份一旦漂移（改 DTS 时
 * 只改一边），灯亮灭就整体反相，而代码里没有任何一处能发现这件事。
 * 现在两份都读、必须一致，不一致就整组拒绝 —— 失败在 board_led_init() 里
 * 落成 BSL_ERR_*，bsl_init_run 记 FAILED，现场能看到"灯没起来"，不是"灯反了"。
 *
 * 全程解析到局部变量，全部成功才提交 s_rt：早前是边解析边写，中途 return 会
 * 留下"led_port 已设、led_count 仍为 0"的半套状态。
 */
static void parse_leds(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_LEDS, 0U);
    GPIO_TypeDef *port = NULL;
    uint16_t pin_mask[RT_MAX_ITEMS];
    int active_low;
    unsigned count;
    unsigned i;

    if (node == NULL) {
        return;
    }
    count = bsl_fdt_child_count(node);
    if (count == 0U) {
        return;
    }
    if (count > RT_MAX_ITEMS) {
        count = RT_MAX_ITEMS;
    }
    active_low = bsl_fdt_prop_flag(node, "active-low");
    for (i = 0U; i < count; i++) {
        GPIO_TypeDef *child_port = NULL;
        unsigned pin = 0U;
        uint32_t flags = 0U;

        if (!gpio_spec(bsl_fdt_child(node, i), &child_port, &pin, &flags)) {
            return; /* 半解析不算数：宁可不点亮也不要错脚 */
        }
        if (((flags != 0U) ? 1 : 0) != (active_low != 0)) {
            return; /* flags 与 active-low 矛盾：数据本身不可信 */
        }
        if (port == NULL) {
            port = child_port;
        } else if (port != child_port) {
            return; /* 同组 LED 必须同端口 */
        }
        pin_mask[i] = (uint16_t)(1U << pin);
    }
    for (i = 0U; i < count; i++) {
        s_rt.led_pin[i] = pin_mask[i];
    }
    s_rt.led_port = port;
    s_rt.led_active_low = active_low;
    s_rt.led_count = count;
}

static void parse_keys(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_KEYS, 0U);
    GPIO_TypeDef *port[RT_MAX_ITEMS];
    uint16_t pin_mask[RT_MAX_ITEMS];
    GPIO_PinState pressed[RT_MAX_ITEMS];
    unsigned count;
    unsigned i;

    if (node == NULL) {
        return;
    }
    count = bsl_fdt_child_count(node);
    if (count == 0U) {
        return;
    }
    if (count > RT_MAX_ITEMS) {
        count = RT_MAX_ITEMS;
    }
    for (i = 0U; i < count; i++) {
        bsl_fdt_node_t key = bsl_fdt_child(node, i);
        GPIO_TypeDef *child_port = NULL;
        unsigned pin = 0U;
        uint32_t flags = 0U;
        int pressed_high;

        if (!gpio_spec(key, &child_port, &pin, &flags)) {
            return;
        }
        /* 同 leds：flags 与 pressed-high / pressed-low 是同一事实的两份拷贝。
         * 约定 flags = 0 为高有效、非 0 为低有效（与生成器一致），矛盾即拒绝。 */
        pressed_high = bsl_fdt_prop_flag(key, "pressed-high");
        if (((flags != 0U) ? 0 : 1) != (pressed_high != 0)) {
            return;
        }
        port[i] = child_port;
        pin_mask[i] = (uint16_t)(1U << pin);
        pressed[i] = (pressed_high != 0) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    for (i = 0U; i < count; i++) {
        s_rt.key_port[i] = port[i];
        s_rt.key_pin[i] = pin_mask[i];
        s_rt.key_pressed[i] = pressed[i];
    }
    s_rt.key_count = count;
}

static void parse_i2c(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_I2C, 0U);
    uint32_t scl[3];
    uint32_t sda[3];
    GPIO_TypeDef *scl_port;
    GPIO_TypeDef *sda_port;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_cells(node, "scl-gpios", scl, 3U) != 3U) {
        return;
    }
    if (bsl_fdt_prop_cells(node, "sda-gpios", sda, 3U) != 3U) {
        return;
    }
    scl_port = board_rt_port_of_phandle(scl[0]);
    sda_port = board_rt_port_of_phandle(sda[0]);
    if (scl_port == NULL || sda_port == NULL) {
        return;
    }
    if (scl[1] > 15U || sda[1] > 15U) {
        return;
    }
    /* 与 leds 同口径：全部校验过才提交 s_rt。早前是边校验边写 s_rt.scl_port /
     * s_rt.sda_port，任一步失败都留下「端口已设、pin 仍是 0、i2c_ok 仍是 0」的半套状态。
     * board_rt_i2c_ready() 能把按 ready 判的消费者挡住，但 board_rt_i2c_scl_port()
     * 会老实返回那个非 NULL 端口 —— 不看 ready 的调用方就会把 I2C 拽到错脚上。 */
    s_rt.scl_port = scl_port;
    s_rt.sda_port = sda_port;
    s_rt.scl_pin = (uint16_t)(1U << scl[1]);
    s_rt.sda_pin = (uint16_t)(1U << sda[1]);
    s_rt.i2c_ok = 1;
}

static void parse_beep(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_BEEP, 0U);
    uint32_t cells[3];
    bsl_fdt_node_t expander;
    uint32_t addr = 0U;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_cells(node, "gpios", cells, 3U) != 3U) {
        return;
    }
    if (cells[1] > 7U) {
        return;
    }
    /* 经 PCF8574 的位驱动：gpios 指向扩展器，flags=1 表示低有效。 */
    expander = bsl_fdt_by_phandle(cells[0]);
    if (expander == NULL) {
        return;
    }
    if (bsl_fdt_prop_u32(expander, "reg", &addr) == 0) {
        return;
    }
    s_rt.pcf8574_addr7 = (uint8_t)(addr & 0x7FU);
    s_rt.beep_bit = (unsigned)cells[1];
    s_rt.beep_active_high = (cells[2] == 0U);
    s_rt.beep_via_pcf8574 = 1;
}

static char first_char(const char *text)
{
    return (text != NULL && text[0] != '\0') ? text[0] : '\0';
}

static void parse_uart(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_UART, 0U);
    uint32_t reg[2];
    uint32_t baud = 0U;
    uint32_t tx_pin = 0U;
    uint32_t rx_pin = 0U;
    char tx_port;
    char rx_port;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_cells(node, "reg", reg, 2U) < 1U) {
        return;
    }
    (void)bsl_fdt_prop_u32(node, "baud", &baud);
    (void)bsl_fdt_prop_u32(node, "tx-pin", &tx_pin);
    (void)bsl_fdt_prop_u32(node, "rx-pin", &rx_pin);
    tx_port = first_char(bsl_fdt_prop_string(node, "tx-port"));
    rx_port = first_char(bsl_fdt_prop_string(node, "rx-port"));
    if (baud == 0U || tx_pin > 15U || rx_pin > 15U || tx_port == '\0'
        || rx_port == '\0') {
        return;
    }
    /* 同 leds / i2c：校验全过才提交。早前 tx_port / rx_port 在 if 之前就写进 s_rt，
     * 校验失败时留下「端口字符已设、baud 为 0、uart_ok 仍为 0」。board_rt_uart()
     * 先查 uart_ok 能挡住，但同样是"失败后还改了一半全局"。 */
    s_rt.uart_reg = (uintptr_t)reg[0];
    s_rt.uart_baud = baud;
    s_rt.tx_pin = (unsigned)tx_pin;
    s_rt.rx_pin = (unsigned)rx_pin;
    s_rt.tx_port = tx_port;
    s_rt.rx_port = rx_port;
    s_rt.uart_ok = 1;
}

/*
 * sdram：FMC Bank5 参数 + 按端口聚合的引脚组。
 *
 * 引脚组格式 `<&gpioX mask AF>`，与 gpios 的三 cell 同形，但第二 cell 是**掩码**：
 * SDRAM 的 30 个脚分属 5 个端口，逐脚写成 30 项既冗长，又会让同一端口被 init 多次。
 * 任一 cell 解不出端口、掩码为 0 或 AF 越界，整组拒绝 —— 半套引脚配置的现场表现是
 * "写得进去读不出来"，而没有任何读数能指出是哪个端口没配上。
 */
static void parse_sdram(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_SDRAM, 0U);
    uint32_t reg[2];
    uint32_t cells[RT_MAX_PIN_GROUPS * 3U];
    uint32_t sdcr = 0U;
    uint32_t sdtr = 0U;
    uint32_t refresh = 0U;
    GPIO_TypeDef *port[RT_MAX_PIN_GROUPS];
    uint16_t mask[RT_MAX_PIN_GROUPS];
    uint32_t af[RT_MAX_PIN_GROUPS];
    unsigned n;
    unsigned groups;
    unsigned i;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_cells(node, "reg", reg, 2U) < 2U) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "sdcr", &sdcr) == 0
        || bsl_fdt_prop_u32(node, "sdtr", &sdtr) == 0
        || bsl_fdt_prop_u32(node, "refresh-count", &refresh) == 0) {
        return;
    }
    n = bsl_fdt_prop_cells(node, "fmc-pins", cells, RT_MAX_PIN_GROUPS * 3U);
    if (n < 3U || (n % 3U) != 0U) {
        return;
    }
    groups = n / 3U;
    for (i = 0U; i < groups; i++) {
        port[i] = board_rt_port_of_phandle(cells[i * 3U]);
        mask[i] = (uint16_t)(cells[i * 3U + 1U] & 0xFFFFU);
        af[i] = cells[i * 3U + 2U];
        if (port[i] == NULL || mask[i] == 0U || af[i] > 15U) {
            return;
        }
    }
    s_rt.sdram_base = (uintptr_t)reg[0];
    s_rt.sdram_size = reg[1];
    s_rt.sdram_sdcr = sdcr;
    s_rt.sdram_sdtr = sdtr;
    s_rt.sdram_refresh = refresh;
    s_rt.sdram_group_count = groups;
    for (i = 0U; i < groups; i++) {
        s_rt.sdram_group_port[i] = port[i];
        s_rt.sdram_group_mask[i] = mask[i];
        s_rt.sdram_group_af[i] = af[i];
    }
    s_rt.sdram_ok = 1;
}

/*
 * panel：LTDC 的时序、像素时钟与背光脚。
 *
 * 与 leds 同一条纪律 —— 全部解析到局部变量，全成功才提交 s_rt。这里失败的代价
 * 更直接：时序缺一项，LTDC 会照样按寄存器里的 0 值出图，现场是"画面整体偏到
 * 左上、右侧一条杂色"，而代码里没有任何读数能指出是 DTB 漏了 hbp。
 *
 * 帧缓冲基址不写死：framebuffer 是 phandle，指向 sdram 节点，基址取它的 reg[0]。
 * 「帧缓存在哪」因此只有一处事实（sdram 的 reg），改 FMC 基址不用动两个地方。
 */
/* 由单一位掩码取位号（调用点已保证只有一位）。 */
static unsigned pin_bit_index(uint16_t mask)
{
    unsigned i;

    for (i = 0U; i < 16U; i++) {
        if ((mask & (uint16_t)(1U << i)) != 0U) {
            return i;
        }
    }
    return 0U;
}

static void parse_panel(void)
{
    bsl_fdt_node_t node;
    bsl_fdt_node_t fb;
    board_rt_panel_t t;
    uint32_t fb_cell[1];
    uint32_t reg[2];
    uint32_t cells[RT_MAX_PIN_GROUPS * 3U];
    uint32_t bl[3];
    uint32_t one[3];
    GPIO_TypeDef *port[RT_MAX_PIN_GROUPS];
    uint16_t mask[RT_MAX_PIN_GROUPS];
    uint32_t af[RT_MAX_PIN_GROUPS];
    GPIO_TypeDef *bl_port = NULL;
    GPIO_TypeDef *ctrl_port[BOARD_RT_LCD_PIN_COUNT];
    unsigned ctrl_pin[BOARD_RT_LCD_PIN_COUNT];
    uint32_t ctrl_af[BOARD_RT_LCD_PIN_COUNT];
    unsigned n;
    unsigned groups = 0U;
    unsigned i;

    /* 先找 MCU 屏、再找 RGB 屏：板上一块屏，命中哪个就由它决定 bus。
     * 两种 compatible 同时存在属于错配置（一份 DTB 描述两块物理屏），
     * 这里以 MCU 那份为准，因为它带着 panel-id 和控制脚，删错哪个都能看出来。 */
    node = bsl_fdt_find(RT_COMPAT_PANEL_MCU, 0U);
    t.bus = BOARD_RT_PANEL_BUS_FMC8080;
    if (node == NULL) {
        node = bsl_fdt_find(RT_COMPAT_PANEL, 0U);
        t.bus = BOARD_RT_PANEL_BUS_RGB;
    }
    if (node == NULL) {
        return;
    }
    t.panel_id = 0U;
    t.hsw = 0U;
    t.hbp = 0U;
    t.hfp = 0U;
    t.vsw = 0U;
    t.vbp = 0U;
    t.vfp = 0U;
    t.pllsai_n = 0U;
    t.pllsai_r = 0U;
    t.pllsai_divr = 0U;

    if (bsl_fdt_prop_u32(node, "width", &t.width) == 0
        || bsl_fdt_prop_u32(node, "height", &t.height) == 0
        || bsl_fdt_prop_u32(node, "pixel-format", &t.pixel_format) == 0) {
        return;
    }
    if (t.width == 0U || t.height == 0U || t.pixel_format > 7U) {
        return;
    }

    if (t.bus == BOARD_RT_PANEL_BUS_RGB) {
        if (bsl_fdt_prop_u32(node, "hsw", &t.hsw) == 0
            || bsl_fdt_prop_u32(node, "hbp", &t.hbp) == 0
            || bsl_fdt_prop_u32(node, "hfp", &t.hfp) == 0
            || bsl_fdt_prop_u32(node, "vsw", &t.vsw) == 0
            || bsl_fdt_prop_u32(node, "vbp", &t.vbp) == 0
            || bsl_fdt_prop_u32(node, "vfp", &t.vfp) == 0
            || bsl_fdt_prop_u32(node, "pllsai-n", &t.pllsai_n) == 0
            || bsl_fdt_prop_u32(node, "pllsai-r", &t.pllsai_r) == 0
            || bsl_fdt_prop_u32(node, "pllsai-divr", &t.pllsai_divr) == 0) {
            return;
        }
        /* 0 在这里不是"某个值"，而是属性没写：同步为 0 的 RGB 面板不存在，
         * 而寄存器里写着 0 的 SSCR 会安静地少出一整行同步。 */
        if (t.hsw == 0U || t.vsw == 0U) {
            return;
        }
        if (t.pllsai_n == 0U || t.pllsai_r == 0U || t.pllsai_divr > 3U) {
            return;
        }
    } else {
        /* MCU 屏：panel-id 用来和读回来的 ID 比对，写 0 等于没判据，所以必须有。 */
        uint32_t pid = 0U;
        if (bsl_fdt_prop_u32(node, "panel-id", &pid) == 0 || pid == 0U) {
            return;
        }
        t.panel_id = pid;
    }

    if (bsl_fdt_prop_cells(node, "framebuffer", fb_cell, 1U) != 1U) {
        return;
    }
    fb = bsl_fdt_by_phandle(fb_cell[0]);
    if (fb == NULL || bsl_fdt_prop_cells(fb, "reg", reg, 2U) < 2U) {
        return;
    }
    t.framebuffer = (uintptr_t)reg[0];

    if (t.bus == BOARD_RT_PANEL_BUS_RGB) {
        n = bsl_fdt_prop_cells(node, "ltdc-pins", cells, RT_MAX_PIN_GROUPS * 3U);
    } else {
        n = bsl_fdt_prop_cells(node, "lcd-data-pins", cells, RT_MAX_PIN_GROUPS * 3U);
    }
    if (n < 3U || (n % 3U) != 0U) {
        return;
    }
    groups = n / 3U;
    for (i = 0U; i < groups; i++) {
        port[i] = board_rt_port_of_phandle(cells[i * 3U]);
        mask[i] = (uint16_t)(cells[i * 3U + 1U] & 0xFFFFU);
        af[i] = cells[i * 3U + 2U];
        if (port[i] == NULL || mask[i] == 0U || af[i] > 15U) {
            return;
        }
    }

    /* 控制脚：每根必须是"单独一位"—— 两位共用一条属性意味着 RS 会跟着 D 线跳。 */
    if (t.bus == BOARD_RT_PANEL_BUS_FMC8080) {
        static const char *const ctrl_prop[BOARD_RT_LCD_PIN_COUNT] = {
            "lcd-cs-pins", "lcd-rs-pins", "lcd-nwe-pins", "lcd-noe-pins"
        };
        for (i = 0U; i < BOARD_RT_LCD_PIN_COUNT; i++) {
            if (bsl_fdt_prop_cells(node, ctrl_prop[i], one, 3U) != 3U) {
                return;
            }
            ctrl_port[i] = board_rt_port_of_phandle(one[0]);
            if (ctrl_port[i] == NULL || one[1] == 0U || (one[1] & (one[1] - 1U)) != 0U
                || one[1] > 0x8000U || one[2] > 15U) {
                return;
            }
            ctrl_pin[i] = pin_bit_index((uint16_t)one[1]);
            ctrl_af[i] = one[2];
        }
    }

    if (bsl_fdt_prop_cells(node, "backlight-gpios", bl, 3U) != 3U) {
        return;
    }
    bl_port = board_rt_port_of_phandle(bl[0]);
    if (bl_port == NULL || bl[1] > 15U) {
        return;
    }

    s_rt.panel = t;
    s_rt.panel_group_count = groups;
    for (i = 0U; i < groups; i++) {
        s_rt.panel_group_port[i] = port[i];
        s_rt.panel_group_mask[i] = mask[i];
        s_rt.panel_group_af[i] = af[i];
    }
    for (i = 0U; i < BOARD_RT_LCD_PIN_COUNT; i++) {
        s_rt.panel_ctrl_port[i] = (t.bus == BOARD_RT_PANEL_BUS_FMC8080) ? ctrl_port[i] : NULL;
        s_rt.panel_ctrl_pin[i] = (t.bus == BOARD_RT_PANEL_BUS_FMC8080) ? ctrl_pin[i] : 0U;
        s_rt.panel_ctrl_af[i] = (t.bus == BOARD_RT_PANEL_BUS_FMC8080) ? ctrl_af[i] : 0U;
    }
    s_rt.panel_bl_port = bl_port;
    s_rt.panel_bl_pin = (unsigned)bl[1];
    /* gpios 第三 cell 与 leds 同一套语义：1 = 低有效。参考工程 LTDC_BL(1) 点亮，
     * 所以 DTS 写 0，这里换算成 active_high。 */
    s_rt.panel_bl_active_high = (bl[2] != 0U) ? 0 : 1;
    s_rt.panel_ok = 1;
}



/*
 * touch：GT9xxx 的地址、复位/中断脚与它那条软件 I2C 的两脚。
 *
 * 为什么触摸总线要单独一组引脚：它挂在 PH6/PI3 上，与主 I2C（pcf8574/蜂鸣器）
 * 不是同一条。早前把"触摸没反应"归到主 I2C 扫描结果上，那条总线上本来就没有
 * 它 —— 结论对不上现场。
 */
static void parse_touch(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_TOUCH, 0U);
    board_rt_touch_t t;
    uint32_t addr7 = 0U;
    uint32_t max_points = 0U;
    uint32_t cells[3];
    GPIO_TypeDef *rst_port = NULL;
    GPIO_TypeDef *scl_port = NULL;
    GPIO_TypeDef *sda_port = NULL;
    GPIO_TypeDef *irq_port = NULL;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "addr7", &addr7) == 0
        || bsl_fdt_prop_u32(node, "max-points", &max_points) == 0) {
        return;
    }
    /* 坐标范围不在这里读：它等于面板像素，唯一来源是 panel 节点的
     * width/height。第二份 800/480 一旦与面板漂移，触点会被整体裁掉或
     * 判成非法，而现场只看到"点右边没反应"。 */
    if (addr7 > 0x7FU || max_points == 0U || max_points > 10U) {
        return;
    }

    /* 三个 gpios 属性各读一次，读完立刻落定到 t 的字段。不复用中间结果 ——
     * 一份 cells 被三组属性轮流覆盖时，第一组的值会被第三组顶掉，而没有任何
     * 读数能指出这件事（触摸"读不到 PID"会被当成硬件问题）。 */
    if (bsl_fdt_prop_cells(node, "reset-gpios", cells, 3U) != 3U) {
        return;
    }
    rst_port = board_rt_port_of_phandle(cells[0]);
    if (rst_port == NULL || cells[1] > 15U) {
        return;
    }
    t.rst_pin = (unsigned)cells[1];
    /* gpios 第三 cell 与 led/panel 同一套语义：1 = 低有效。GT9xxx 的 RST 是
     * 低电平复位，DTB 写 1。 */
    t.rst_active_low = (cells[2] != 0U) ? 1 : 0;

    if (bsl_fdt_prop_cells(node, "scl-gpios", cells, 3U) != 3U) {
        return;
    }
    scl_port = board_rt_port_of_phandle(cells[0]);
    if (scl_port == NULL || cells[1] > 15U) {
        return;
    }
    t.scl_pin = (unsigned)cells[1];

    if (bsl_fdt_prop_cells(node, "sda-gpios", cells, 3U) != 3U) {
        return;
    }
    sda_port = board_rt_port_of_phandle(cells[0]);
    if (sda_port == NULL || cells[1] > 15U) {
        return;
    }
    t.sda_pin = (unsigned)cells[1];

    /* irq 脚可选：不接中断也能轮询。 */
    t.irq_port = NULL;
    t.irq_pin = 0U;
    if (bsl_fdt_prop_cells(node, "irq-gpios", cells, 3U) == 3U) {
        irq_port = board_rt_port_of_phandle(cells[0]);
        if (irq_port != NULL && cells[1] <= 15U) {
            t.irq_port = irq_port;
            t.irq_pin = (unsigned)cells[1];
        }
    }

    t.addr7 = (uint8_t)addr7;
    t.max_points = (unsigned)max_points;
    t.rst_port = rst_port;
    t.scl_port = scl_port;
    t.sda_port = sda_port;
    s_rt.touch = t;
    s_rt.touch_ok = 1;
}

/*
 * nor：W25Q256 挂在 SPI5（SCK=PF7 / MISO=PF8 / MOSI=PF9，AF5），片选 PF6。
 *
 * 三根信号线各读一次、各自落定 —— 与 touch 同一口径：共用一份 cells 时
 * 后读会顶掉先读，三根线互换的直接后果是"ID 读成 0xFF 或 0x00"，而没有任何
 * 读数能指出是哪一根错了。
 *
 * 容量/页/扇区也来自 DTB 而不是代码常量：擦写粒度错了的后果是"读能读到、
 * 写进去再读变成 0xFF"，比读不到更难查。
 */
static void parse_nor(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_NOR, 0U);
    board_rt_nor_t n;
    uint32_t inst_no = 0U;
    uint32_t size = 0U;
    uint32_t page = 0U;
    uint32_t sector = 0U;
    uint32_t cells[3];
    GPIO_TypeDef *port;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "spi-inst", &inst_no) == 0
        || bsl_fdt_prop_u32(node, "size", &size) == 0
        || bsl_fdt_prop_u32(node, "page-size", &page) == 0
        || bsl_fdt_prop_u32(node, "sector-size", &sector) == 0) {
        return;
    }
    switch (inst_no) {
    case 1U: n.inst = SPI1; break;
    case 2U: n.inst = SPI2; break;
    case 3U: n.inst = SPI3; break;
    case 4U: n.inst = SPI4; break;
    case 5U: n.inst = SPI5; break;
    case 6U: n.inst = SPI6; break;
    default: return;
    }
    if (size == 0U || page == 0U || sector == 0U || (size % sector) != 0U
        || (sector % page) != 0U) {
        return;
    }

    if (bsl_fdt_prop_cells(node, "sck", cells, 3U) != 3U) {
        return;
    }
    port = board_rt_port_of_phandle(cells[0]);
    if (port == NULL || cells[1] > 15U || cells[2] > 15U) {
        return;
    }
    n.sck_port = port;
    n.sck_pin = (unsigned)cells[1];
    n.af = (unsigned)cells[2];

    if (bsl_fdt_prop_cells(node, "miso", cells, 3U) != 3U) {
        return;
    }
    port = board_rt_port_of_phandle(cells[0]);
    if (port == NULL || cells[1] > 15U || cells[2] != n.af) {
        return;
    }
    n.miso_port = port;
    n.miso_pin = (unsigned)cells[1];

    if (bsl_fdt_prop_cells(node, "mosi", cells, 3U) != 3U) {
        return;
    }
    port = board_rt_port_of_phandle(cells[0]);
    if (port == NULL || cells[1] > 15U || cells[2] != n.af) {
        return;
    }
    n.mosi_port = port;
    n.mosi_pin = (unsigned)cells[1];

    if (bsl_fdt_prop_cells(node, "cs-gpios", cells, 3U) != 3U) {
        return;
    }
    port = board_rt_port_of_phandle(cells[0]);
    if (port == NULL || cells[1] > 15U) {
        return;
    }
    n.cs_port = port;
    n.cs_pin = (unsigned)cells[1];
    n.cs_active_low = (cells[2] != 0U) ? 1 : 0;

    n.size = size;
    n.page_size = page;
    n.sector_size = sector;
    s_rt.nor = n;
    s_rt.nor_ok = 1;
}

/*
 * rtc：走时源与 LSE 频率是**板级事实**（晶振焊没焊、焊的是 32.768 k 还是 32.000 k）。
 * 寄存器基址是芯片事实，不写 DTB。
 *
 * clock-source 只认 "lse" / "lsi" 两个串：写别的（拼错、写成 "external"）时整节点
 * 拒绝，而不是当作默认 LSE —— 猜错的代价是 LSE 起振白等 3 s、且降级到 LSI 后
 * 精度掉到分钟级，现场却看不出来。
 */
static void parse_rtc(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_RTC, 0U);
    const char *src;
    board_rt_rtc_t r;
    uint32_t hz = 0U;

    if (node == NULL) {
        return;
    }
    src = bsl_fdt_prop_string(node, "clock-source");
    if (src == NULL) {
        return;
    }
    if (strcmp(src, "lsi") == 0) {
        r.use_lsi = 1;
    } else if (strcmp(src, "lse") == 0) {
        r.use_lsi = 0;
    } else {
        return;
    }
    if (bsl_fdt_prop_u32(node, "lse-frequency", &hz) == 0) {
        hz = 0U; /* 0 = 未声明，bsl_rtc 按 32768 兜底 */
    }
    r.lse_hz = hz;
    s_rt.rtc = r;
    s_rt.rtc_ok = 1;
}

/*
 * adc-temp：只有 VREF+ 是板级事实。内部温度通道的通道号（16）与采样时间是
 * 手册事实，写在驱动里；校准值 TS_CAL1/TS_CAL2 是芯片出厂值，从系统存储区读。
 *
 * vref-mv 是**校准有效性的门限**而不是一个摆设读数：数据手册的 TS_CAL 值只在
 * VDDA=VREF+=3.3 V ±0.1 V 下成立，电压偏离时换算出的温度会整体偏移，而现场
 * 只表现为"温度偏高几度"，没有任何读数指向电源。荒数（<1.5 V / >3.6 V）直接
 * 拒收。
 */
static void parse_adc(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_ADC, 0U);
    uint32_t mv = 0U;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "vref-mv", &mv) == 0) {
        return;
    }
    if (mv < 1500U || mv > 3600U) {
        return;
    }
    s_rt.adc.vref_mv = mv;
    s_rt.adc_ok = 1;
}

/*
 * sai：音频播放链（SAI1 block A + 挂在软件 I2C 上的 WM8978）。
 *
 * 与 panel 同一条纪律：全部解析到局部变量，全成功才提交。这里失败的现场是
 * "一点声音都没有"，比面板偏色更难查 —— 没有任何读数指向"DTB 里 pll2-q 少写了"。
 *
 * codec 是 phandle：地址只写一次（wm8978 节点的 reg），sai 节点引用它。
 * 没挂 codec 时 codec_addr7 留 0，board_wm8978 据此拒绝初始化，不去猜 0x1A。
 */
static void parse_sai(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_SAI, 0U);
    bsl_fdt_node_t codec;
    board_rt_sai_t t;
    uint32_t cells[RT_MAX_PIN_GROUPS * 3U];
    uint32_t ph[1];
    uint32_t reg[2];
    GPIO_TypeDef *port[RT_MAX_PIN_GROUPS];
    uint16_t mask[RT_MAX_PIN_GROUPS];
    uint32_t af[RT_MAX_PIN_GROUPS];
    unsigned n;
    unsigned groups;
    unsigned i;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "sample-rate", &t.sample_rate) == 0
        || bsl_fdt_prop_u32(node, "mck-div", &t.mck_div) == 0
        || bsl_fdt_prop_u32(node, "pll2-n", &t.pll2_n) == 0
        || bsl_fdt_prop_u32(node, "pll2-q", &t.pll2_q) == 0
        || bsl_fdt_prop_u32(node, "pll2-divq", &t.pll2_divq) == 0) {
        return;
    }
    /* 范围按手册与本工程时钟卡住，不放过"看着像参数"的值：
     *   FS 8k..96k（WM8978 的 MCLK=256fs 方案支持的范围）、
     *   MCKDIV 1..15（CR1 字段 4 位，0 等于不分频、会超 SAI 时钟上限）、
     *   PLLI2S N 50..432（1 MHz 输入下 VCO 必须落在 100..432 MHz，N=50 只到 50 MHz
     *   会被下方 100 MHz 下限挡掉，这里先保住字段宽度）、Q 2..15、DIVQ 0..31。 */
    if (t.sample_rate < 8000U || t.sample_rate > 96000U) {
        return;
    }
    if (t.mck_div < 1U || t.mck_div > 15U) {
        return;
    }
    if (t.pll2_n < 50U || t.pll2_n > 432U) {
        return;
    }
    if (t.pll2_q < 2U || t.pll2_q > 15U) {
        return;
    }
    if (t.pll2_divq > 31U) {
        return;
    }
    n = bsl_fdt_prop_cells(node, "sai-pins", cells, RT_MAX_PIN_GROUPS * 3U);
    if (n < 3U || (n % 3U) != 0U) {
        return;
    }
    groups = n / 3U;
    for (i = 0U; i < groups; i++) {
        port[i] = board_rt_port_of_phandle(cells[i * 3U]);
        mask[i] = (uint16_t)(cells[i * 3U + 1U] & 0xFFFFU);
        af[i] = cells[i * 3U + 2U];
        if (port[i] == NULL || mask[i] == 0U || af[i] > 15U) {
            return;
        }
    }

    t.codec_addr7 = 0U;
    if (bsl_fdt_prop_cells(node, "codec", ph, 1U) == 1U) {
        codec = bsl_fdt_by_phandle(ph[0]);
        if (codec != NULL && bsl_fdt_prop_cells(codec, "reg", reg, 1U) == 1U
            && reg[0] < 0x78U && reg[0] >= 0x08U) {
            t.codec_addr7 = (uint8_t)reg[0];
        }
    }

    s_rt.sai = t;
    s_rt.sai_group_count = groups;
    for (i = 0U; i < groups; i++) {
        s_rt.sai_group_port[i] = port[i];
        s_rt.sai_group_mask[i] = mask[i];
        s_rt.sai_group_af[i] = af[i];
    }
    s_rt.sai_ok = 1;
}

/* 节点名与 compatible 是两件事：name 决定 /sys 里怎么称呼这颗器件，
 * compatible 决定谁来驱动。四个传感器节点的节点名在对照表 P6d 行定死
 * （`imu@68` / `als@1e` / `eeprom@50` / `tpad`），这里只认 compatible。 */
#define RT_COMPAT_IMU "invensense,mpu9250"
#define RT_COMPAT_ALS "liteon,ap3216c"
#define RT_COMPAT_EEPROM "atmel,24c02"
#define RT_COMPAT_TPAD "brd,tpad"

/* 触发器实例名 → 指针。只收录本工程真用到的：P6d 的 TPAD 用 TIM2，
 * P6e 的 PWM 会用别的定时器，那时再往这张表里加一行，而不是让每个驱动
 * 各自写一份 if-else。 */
static TIM_TypeDef *timer_of_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, "tim2") == 0) {
        return TIM2;
    }
    if (strcmp(name, "tim3") == 0) {
        return TIM3;
    }
    if (strcmp(name, "tim4") == 0) {
        return TIM4;
    }
    if (strcmp(name, "tim5") == 0) {
        return TIM5;
    }
    return NULL;
}

/* I2C 传感器只解析 7 位地址。地址合法区间是 0x08..0x77（0x00..0x07 与 0x78..0x7F
 * 是 I2C 规范的保留段）：写进 DTB 的地址若落在这两段，节点整条拒收，
 * 而不是"照抄着发出去"，让现场变成"偶发 NACK、不知道是地址错还是器件错"。 */
static int sensor_addr7(bsl_fdt_node_t node, uint8_t *out)
{
    uint32_t reg = 0U;

    if (node == NULL) {
        return 0;
    }
    if (bsl_fdt_prop_cells(node, "reg", &reg, 1U) != 1U) {
        return 0;
    }
    if (reg < 0x08U || reg > 0x77U) {
        return 0;
    }
    *out = (uint8_t)reg;
    return 1;
}

/* imu：主地址 + 磁力计地址。mag-addr 缺失不算失败（磁力计自检降级），
 * 但主地址缺失整节点拒收 —— 没有主地址连 WHO_AM_I 都读不到。 */
static void parse_mpu9250(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_IMU, 0U);
    board_rt_mpu9250_t m;
    uint32_t mag = 0U;

    if (sensor_addr7(node, &m.addr7) == 0) {
        return;
    }
    m.mag_addr7 = 0U;
    if (bsl_fdt_prop_u32(node, "mag-addr", &mag) != 0 && mag >= 0x08U && mag <= 0x77U) {
        m.mag_addr7 = (uint8_t)mag;
    }
    s_rt.imu = m;
    s_rt.imu_ok = 1;
}

static void parse_ap3216c(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_ALS, 0U);
    board_rt_ap3216c_t a;

    if (sensor_addr7(node, &a.addr7) == 0) {
        return;
    }
    s_rt.als = a;
    s_rt.als_ok = 1;
}

static void parse_eeprom(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_EEPROM, 0U);
    board_rt_eeprom_t e;

    if (sensor_addr7(node, &e.addr7) == 0) {
        return;
    }
    s_rt.eeprom = e;
    s_rt.eeprom_ok = 1;
}

/* tpad：引脚 + 定时器 + 通道 + 计数/门限参数。
 *
 * 通道号与"捕获上升沿"是驱动事实（写死在 board_tpad 里），不是板级事实；
 * 这里给的是"用哪个脚、哪个定时器、几号通道、计数上限多少、放电多久、
 * 判定门限多少" —— 换板子/换定时器只改 DTS，不动驱动。
 *
 * 校验口径取自参考工程 tpad_init(psc) 的实际取值域：prescaler >= 1（0 会除零）、
 * arr_max 必须 > 1000（否则超时判据与"基线太大"判据都失去意义）、charge_ms 取 1..20
 * （参考工程用 5 ms，放电不足会让读数整体偏低，放电过久只是慢）。 */
static void parse_tpad(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_TPAD, 0U);
    board_rt_tpad_t t;
    uint32_t cells[3];
    const char *tim;
    uint32_t ch = 0U;
    uint16_t mask;
    unsigned pin;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_cells(node, "tpad-pins", cells, 3U) != 3U) {
        return;
    }
    t.port = board_rt_port_of_phandle(cells[0]);
    if (t.port == NULL || cells[2] > 15U) {
        return;
    }
    /* cells[1] 是**掩码**，与 sai-pins / panel 的引脚组属性同一口径（不是脚号）。
     * 必须恰好一位：TPAD 只有一根脚，给 0x0021（两根）说明 DTS 写错了，
     * 直接拒收 —— 拿掩码当"脚号"或反之，都会得到一个看似合法、实际指错脚的配置。 */
    mask = (uint16_t)(cells[1] & 0xFFFFU);
    if (mask == 0U || (mask & (uint16_t)(mask - 1U)) != 0U) {
        return;
    }
    for (pin = 0U; pin < 16U; pin++) {
        if ((mask & (uint16_t)(1U << pin)) != 0U) {
            break;
        }
    }
    t.pin = pin;
    t.af = cells[2];
    tim = bsl_fdt_prop_string(node, "timer");
    t.inst = timer_of_name(tim);
    if (t.inst == NULL) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "channel", &ch) == 0 || ch < 1U || ch > 2U) {
        /* 只收 1..2：这两个通道落在 CCMR1 里，3/4 落 CCMR2，驱动分两套写法；
         * 本板用 CH1，多留一个 CH2 的空间，3/4 等真有需求再放开。 */
        return;
    }
    t.channel = (unsigned)ch;
    if (bsl_fdt_prop_u32(node, "arr-max", &t.arr_max) == 0 || t.arr_max < 1000U || t.arr_max > 0xFFFFU) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "prescaler", &t.prescaler) == 0 || t.prescaler < 1U || t.prescaler > 1024U) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "charge-ms", &t.charge_ms) == 0
        || t.charge_ms < 1U || t.charge_ms > 20U) {
        return;
    }
    s_rt.tpad = t;
    s_rt.tpad_ok = 1;
}

/* can / usart3（P6e-1）：引脚属性与 sai-pins 同格式 `<phandle mask af>`。
 *
 * 统一在这里把"掩码必须是 N 位"的检查做掉：CAN 要 RX+TX 两位（本板都在 PA），
 * USART 要 TX+RX 两位。少于两位不是"少一根脚无所谓" —— USART 少 RX 就是
 * "发得出去、收不回来"，现场表现成"对端没回话"，比节点缺失难查得多。 */
#define RT_COMPAT_CAN "brd,can"
#define RT_COMPAT_DMA "brd,dma"
#define RT_COMPAT_PWM "brd,pwm"
#define RT_COMPAT_EXTI "brd,exti"
#define RT_COMPAT_NAND "brd,nand"
#define RT_COMPAT_USART "brd,usart"
#define RT_COMPAT_DAC "brd,dac"

static int pin_group(bsl_fdt_node_t node, const char *prop, GPIO_TypeDef **port,
                     uint16_t *mask, unsigned *af, unsigned bits)
{
    uint32_t cells[3];
    unsigned n = 0U;
    unsigned i;

    if (node == NULL || prop == NULL) {
        return 0;
    }
    if (bsl_fdt_prop_cells(node, prop, cells, 3U) != 3U) {
        return 0;
    }
    *port = board_rt_port_of_phandle(cells[0]);
    if (*port == NULL || cells[2] > 15U) {
        return 0;
    }
    *mask = (uint16_t)(cells[1] & 0xFFFFU);
    *af = cells[2];
    for (i = 0U; i < 16U; i++) {
        if ((*mask & (uint16_t)(1U << i)) != 0U) {
            n++;
        }
    }
    return (n == bits) ? 1 : 0;
}

static void parse_can(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_CAN, 0U);
    board_rt_can_t c;

    if (pin_group(node, "can-pins", &c.port, &c.mask, &c.af, 2U) == 0) {
        return;
    }
    /* 位率区间按 CAN 的常规档位收：10 kbit/s ~ 1 Mbit/s。超出这个区间先怀疑
     * DTS 写错了单位（写成 <500000000> 之类），而不是让驱动算出一个
     * brp 超出 10 位域、寄存器被截断、最后位率变成另一个数的配置。 */
    if (bsl_fdt_prop_u32(node, "bitrate", &c.bitrate) == 0
        || c.bitrate < 10000U || c.bitrate > 1000000U) {
        return;
    }
    c.loopback = bsl_fdt_prop_flag(node, "loopback");
    s_rt.can = c;
    s_rt.can_ok = 1;
}

static void parse_usart(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_USART, 0U);
    board_rt_usart_t u;

    if (pin_group(node, "usart-pins", &u.port, &u.mask, &u.af, 2U) == 0) {
        return;
    }
    /* 波特率上限按 PCLK1/16 留余量（本板 45 MHz ⇒ 理论 2.8 Mbit/s 少一点），
     * 下限取 1200：低于这个数 BRR 会超过 16 位域，写进去的是截断值。 */
    if (bsl_fdt_prop_u32(node, "baud", &u.baud) == 0
        || u.baud < 1200U || u.baud > 2000000U) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "frame-idle-ms", &u.frame_idle_ms) == 0
        || u.frame_idle_ms < 1U || u.frame_idle_ms > 100U) {
        return;
    }
    s_rt.usart = u;
    s_rt.usart_ok = 1;
}

/* nand（P9a）：几何全部从 DTB 读；R/B 脚只作事实登记（本批读通道走状态
 * 寄存器，不依赖 R/B 电平 —— 见 board_nand.c 的注释）。 */
static void parse_nand(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_NAND, 0U);
    board_rt_nand_t n;
    uint32_t page_main = 0U;
    uint32_t page_spare = 0U;
    uint32_t block_pages = 0U;
    uint32_t blocks = 0U;
    uint32_t rb[3];

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "page-size", &page_main) == 0
        || bsl_fdt_prop_u32(node, "spare-size", &page_spare) == 0
        || bsl_fdt_prop_u32(node, "block-pages", &block_pages) == 0
        || bsl_fdt_prop_u32(node, "blocks", &blocks) == 0) {
        return;
    }
    if (page_main == 0U || page_main > 0xFFFFU
        || page_spare == 0U || page_spare > 0xFFFFU
        || block_pages == 0U || blocks == 0U) {
        return;
    }
    n.page_main = (uint16_t)page_main;
    n.page_spare = (uint16_t)page_spare;
    n.block_pages = block_pages;
    n.blocks = blocks;
    n.rb_port = NULL;
    n.rb_mask = 0U;
    if (bsl_fdt_prop_cells(node, "rb-gpios", rb, 3U) == 3U) {
        n.rb_port = board_rt_port_of_phandle(rb[0]);
        if (n.rb_port != NULL && rb[1] <= 15U) {
            n.rb_mask = (uint16_t)(1U << rb[1]);
        }
    }
    s_rt.nand = n;
    s_rt.nand_ok = 1;
}
static void parse_dma(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_DMA, 0U);
    board_rt_dma_t d;
    uint32_t inst = 0U;
    uint32_t stream = 0U;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "instance", &inst) == 0
        || bsl_fdt_prop_u32(node, "stream", &stream) == 0) {
        return;
    }
    d.instance = inst;
    d.stream = stream;
    s_rt.dma = d;
    s_rt.dma_ok = 1;
}

/* 端口序号（写进 SYSCFG EXTICR 的值）：A=0 … E=4。用真实的 GPIO 指针反查，
 * 不接受 DTS 里另写一个"端口字母"——两份拷贝迟早漂移，而现象是"中断永远不来"。 */
static unsigned port_index(GPIO_TypeDef *port)
{
    switch ((uintptr_t)port) {
    case GPIOA_BASE: return 0U;
    case GPIOB_BASE: return 1U;
    case GPIOC_BASE: return 2U;
    case GPIOD_BASE: return 3U;
    case GPIOE_BASE: return 4U;
    default: return 0xFFU;
    }
}

/* pwm：`pwm-pins = <&gpiob 0x0020 2>` + `timer = "tim3"` + `channel` + `frequency`。
 * 第二个 cell 是**掩码**（与 tpad-pins / sai-pins 同口径），必须恰好一位：
 * 给两根脚说明 DTS 写错了，此时"取最低位"会悄悄配到另一根脚上。 */
static void parse_pwm(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_PWM, 0U);
    board_rt_pwm_t p;
    uint32_t cells[3];
    const char *tim;
    uint32_t ch = 0U;
    uint32_t freq = 0U;
    uint16_t mask;
    unsigned pin;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_cells(node, "pwm-pins", cells, 3U) != 3U) {
        return;
    }
    p.port = board_rt_port_of_phandle(cells[0]);
    if (p.port == NULL || cells[2] > 15U) {
        return;
    }
    mask = (uint16_t)(cells[1] & 0xFFFFU);
    if (mask == 0U || (mask & (uint16_t)(mask - 1U)) != 0U) {
        return;
    }
    for (pin = 0U; pin < 16U; pin++) {
        if ((mask & (uint16_t)(1U << pin)) != 0U) {
            break;
        }
    }
    p.pin = pin;
    p.af = cells[2];
    tim = bsl_fdt_prop_string(node, "timer");
    p.inst = timer_of_name(tim);
    if (p.inst == NULL) {
        return;
    }
    if (bsl_fdt_prop_u32(node, "channel", &ch) == 0 || ch < 1U || ch > 2U) {
        return;
    }
    p.channel = (unsigned)ch;
    if (bsl_fdt_prop_u32(node, "frequency", &freq) == 0
        || freq < 100U || freq > 20000U) {
        /* 100 Hz..20 kHz：低于 100 Hz 背光可见闪烁，高于 20 kHz 已超出
         * 本板背光使能回路的响应，两个方向都没有意义。 */
        return;
    }
    p.frequency = freq;
    s_rt.pwm = p;
    s_rt.pwm_ok = 1;
}

/* exti：`exti-gpios = <&gpioa 0 0>`（gpios 口径：第二个 cell 是**脚号**，
 * 不是掩码）+ `trigger = "rising"|"falling"`。line 按脚号映射是芯片固定事实。 */
static void parse_exti(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_EXTI, 0U);
    board_rt_exti_t e;
    uint32_t cells[3];
    const char *trig;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_cells(node, "exti-gpios", cells, 3U) != 3U) {
        return;
    }
    e.port = board_rt_port_of_phandle(cells[0]);
    if (e.port == NULL || cells[1] > 15U) {
        return;
    }
    e.pin = (unsigned)cells[1];
    e.line = e.pin;
    if (e.line > 4U) {
        /* 5..15 共用 EXTI9_5 / EXTI15_10 两条向量，驱动没做；DTB 指到那里
         * 就整节点拒收，避免"注册了但永远进不来中断"。 */
        return;
    }
    e.src = port_index(e.port);
    if (e.src > 4U) {
        return;
    }
    trig = bsl_fdt_prop_string(node, "trigger");
    if (trig == NULL) {
        return;
    }
    if (strcmp(trig, "rising") == 0) {
        e.rising = 1;
    } else if (strcmp(trig, "falling") == 0) {
        e.rising = 0;
    } else {
        return;
    }
    s_rt.exti = e;
    s_rt.exti_ok = 1;
}

/* dac：`dac-pins = <&gpioa 0x0010>`（掩码口径，与 pwm-pins 同）+ `channel = <1>`。
 * 掩码必须恰好一位：给两根脚说明 DTS 写错了，"取最低位"会静默配到另一根脚上。
 * channel 只认 1 —— DAC1_OUT2 是 PA5，本板 PA5 归 TPAD 的 TIM2_CH1，写 2 就撞脚。 */
static void parse_dac(void)
{
    bsl_fdt_node_t node = bsl_fdt_find(RT_COMPAT_DAC, 0U);
    board_rt_dac_t d;
    uint32_t cells[2];
    uint32_t ch = 0U;
    uint16_t mask;
    unsigned pin;

    if (node == NULL) {
        return;
    }
    if (bsl_fdt_prop_cells(node, "dac-pins", cells, 2U) != 2U) {
        return;
    }
    d.port = board_rt_port_of_phandle(cells[0]);
    if (d.port == NULL) {
        return;
    }
    mask = (uint16_t)(cells[1] & 0xFFFFU);
    if (mask == 0U || (mask & (uint16_t)(mask - 1U)) != 0U) {
        return;
    }
    for (pin = 0U; pin < 16U; pin++) {
        if ((mask & (uint16_t)(1U << pin)) != 0U) {
            break;
        }
    }
    d.pin = pin;
    if (bsl_fdt_prop_u32(node, "channel", &ch) == 0 || ch != 1U) {
        return;
    }
    d.channel = (unsigned)ch;
    s_rt.dac = d;
    s_rt.dac_ok = 1;
}

static void parse_all(void)
{
    if (s_rt.parsed) {
        return;
    }
    s_rt.parsed = 1;
    if (bsl_fdt_init() != 0) {
        return;
    }
    parse_clocks();
    parse_sdram();
    parse_panel();
    parse_touch();
    parse_nor();
    parse_leds();
    parse_keys();
    parse_i2c();
    parse_beep();
    parse_uart();
    parse_rtc();
    parse_adc();
    parse_sai();
    parse_mpu9250();
    parse_ap3216c();
    parse_eeprom();
    parse_tpad();
    parse_can();
    parse_usart();
    parse_nand();
    parse_dma();
    parse_pwm();
    parse_exti();
    parse_dac();
}

uint32_t board_rt_hse_hz(void)
{
    parse_all();
    return s_rt.hse_hz;
}

int board_rt_can(board_rt_can_t *out)
{
    parse_all();
    if (!s_rt.can_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.can;
    return 1;
}

int board_rt_usart(board_rt_usart_t *out)
{
    parse_all();
    if (!s_rt.usart_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.usart;
    return 1;
}

int board_rt_dma(board_rt_dma_t *out)
{
    parse_all();
    if (!s_rt.dma_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.dma;
    return 1;
}

int board_rt_pwm(board_rt_pwm_t *out)
{
    parse_all();
    if (!s_rt.pwm_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.pwm;
    return 1;
}

int board_rt_pwm_is_backlight(void)
{
    parse_all();
    if (!s_rt.pwm_ok || !s_rt.panel_ok) {
        return 0;
    }
    /* 端口指针与脚号都来自 DTB；面板没写 backlight-gpios 时 s_rt.panel_bl_port
     * 是 NULL，比较自然失败 → 背光留在 PWM 自己的默认占空比上。 */
    return (s_rt.pwm.port == s_rt.panel_bl_port
            && s_rt.pwm.pin == s_rt.panel_bl_pin) ? 1 : 0;
}

int board_rt_exti(board_rt_exti_t *out)
{
    parse_all();
    if (!s_rt.exti_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.exti;
    return 1;
}

int board_rt_nand(board_rt_nand_t *out)
{
    parse_all();
    if (!s_rt.nand_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.nand;
    return 1;
}
unsigned board_rt_led_count(void)
{
    parse_all();
    return s_rt.led_count;
}

GPIO_TypeDef *board_rt_led_port(void)
{
    parse_all();
    return s_rt.led_port;
}

uint16_t board_rt_led_pin(unsigned index)
{
    parse_all();
    return (index < s_rt.led_count) ? s_rt.led_pin[index] : 0U;
}

int board_rt_led_active_low(void)
{
    parse_all();
    return s_rt.led_active_low;
}

unsigned board_rt_key_count(void)
{
    parse_all();
    return s_rt.key_count;
}

GPIO_TypeDef *board_rt_key_port(unsigned index)
{
    parse_all();
    return (index < s_rt.key_count) ? s_rt.key_port[index] : NULL;
}

uint16_t board_rt_key_pin(unsigned index)
{
    parse_all();
    return (index < s_rt.key_count) ? s_rt.key_pin[index] : 0U;
}

GPIO_PinState board_rt_key_pressed(unsigned index)
{
    parse_all();
    return (index < s_rt.key_count) ? s_rt.key_pressed[index] : GPIO_PIN_RESET;
}

GPIO_TypeDef *board_rt_i2c_scl_port(void)
{
    parse_all();
    return s_rt.scl_port;
}

uint16_t board_rt_i2c_scl_pin(void)
{
    parse_all();
    return s_rt.scl_pin;
}

GPIO_TypeDef *board_rt_i2c_sda_port(void)
{
    parse_all();
    return s_rt.sda_port;
}

uint16_t board_rt_i2c_sda_pin(void)
{
    parse_all();
    return s_rt.sda_pin;
}

int board_rt_i2c_ready(void)
{
    parse_all();
    return s_rt.i2c_ok;
}

int board_rt_beep_via_pcf8574(void)
{
    parse_all();
    return s_rt.beep_via_pcf8574;
}

uint8_t board_rt_pcf8574_addr7(void)
{
    parse_all();
    return s_rt.pcf8574_addr7;
}

unsigned board_rt_beep_bit(void)
{
    parse_all();
    return s_rt.beep_bit;
}

int board_rt_beep_active_high(void)
{
    parse_all();
    return s_rt.beep_active_high;
}

int board_rt_uart(uintptr_t *reg, uint32_t *baud, char *tx_port,
                  unsigned *tx_pin, char *rx_port, unsigned *rx_pin)
{
    parse_all();
    if (!s_rt.uart_ok) {
        return 0;
    }
    if (reg != NULL) { *reg = s_rt.uart_reg; }
    if (baud != NULL) { *baud = s_rt.uart_baud; }
    if (tx_port != NULL) { *tx_port = s_rt.tx_port; }
    if (tx_pin != NULL) { *tx_pin = s_rt.tx_pin; }
    if (rx_port != NULL) { *rx_port = s_rt.rx_port; }
    if (rx_pin != NULL) { *rx_pin = s_rt.rx_pin; }
    return 1;
}

int board_rt_sdram(uintptr_t *base, uint32_t *size, uint32_t *sdcr,
                   uint32_t *sdtr, uint32_t *refresh_count)
{
    parse_all();
    if (!s_rt.sdram_ok) {
        return 0;
    }
    if (base != NULL) { *base = s_rt.sdram_base; }
    if (size != NULL) { *size = s_rt.sdram_size; }
    if (sdcr != NULL) { *sdcr = s_rt.sdram_sdcr; }
    if (sdtr != NULL) { *sdtr = s_rt.sdram_sdtr; }
    if (refresh_count != NULL) { *refresh_count = s_rt.sdram_refresh; }
    return 1;
}

unsigned board_rt_sdram_pin_groups(void)
{
    parse_all();
    return s_rt.sdram_group_count;
}

GPIO_TypeDef *board_rt_sdram_pin_port(unsigned index)
{
    parse_all();
    return (index < s_rt.sdram_group_count) ? s_rt.sdram_group_port[index] : NULL;
}

uint16_t board_rt_sdram_pin_mask(unsigned index)
{
    parse_all();
    return (index < s_rt.sdram_group_count) ? s_rt.sdram_group_mask[index] : 0U;
}

uint32_t board_rt_sdram_pin_af(unsigned index)
{
    parse_all();
    return (index < s_rt.sdram_group_count) ? s_rt.sdram_group_af[index] : 0U;
}

int board_rt_panel(board_rt_panel_t *out)
{
    parse_all();
    if (!s_rt.panel_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.panel;
    return 1;
}

unsigned board_rt_panel_pin_groups(void)
{
    parse_all();
    return s_rt.panel_group_count;
}

GPIO_TypeDef *board_rt_panel_pin_port(unsigned index)
{
    parse_all();
    return (index < s_rt.panel_group_count) ? s_rt.panel_group_port[index] : NULL;
}

uint16_t board_rt_panel_pin_mask(unsigned index)
{
    parse_all();
    return (index < s_rt.panel_group_count) ? s_rt.panel_group_mask[index] : 0U;
}

uint32_t board_rt_panel_pin_af(unsigned index)
{
    parse_all();
    return (index < s_rt.panel_group_count) ? s_rt.panel_group_af[index] : 0U;
}

int board_rt_panel_backlight(GPIO_TypeDef **port, unsigned *pin, int *active_high)
{
    parse_all();
    if (!s_rt.panel_ok) {
        return 0;
    }
    if (port != NULL) { *port = s_rt.panel_bl_port; }
    if (pin != NULL) { *pin = s_rt.panel_bl_pin; }
    if (active_high != NULL) { *active_high = s_rt.panel_bl_active_high; }
    return 1;
}

int board_rt_touch(board_rt_touch_t *out)
{
    parse_all();
    if (!s_rt.touch_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.touch;
    return 1;
}

int board_rt_nor(board_rt_nor_t *out)
{
    parse_all();
    if (!s_rt.nor_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.nor;
    return 1;
}

int board_rt_rtc(board_rt_rtc_t *out)
{
    parse_all();
    if (!s_rt.rtc_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.rtc;
    return 1;
}

int board_rt_adc_temp(board_rt_adc_t *out)
{
    parse_all();
    if (!s_rt.adc_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.adc;
    return 1;
}

int board_rt_sai(board_rt_sai_t *out)
{
    parse_all();
    if (!s_rt.sai_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.sai;
    return 1;
}

unsigned board_rt_sai_pin_groups(void)
{
    parse_all();
    return s_rt.sai_group_count;
}

GPIO_TypeDef *board_rt_sai_pin_port(unsigned index)
{
    parse_all();
    return (index < s_rt.sai_group_count) ? s_rt.sai_group_port[index] : NULL;
}

uint16_t board_rt_sai_pin_mask(unsigned index)
{
    parse_all();
    return (index < s_rt.sai_group_count) ? s_rt.sai_group_mask[index] : 0U;
}

uint32_t board_rt_sai_pin_af(unsigned index)
{
    parse_all();
    return (index < s_rt.sai_group_count) ? s_rt.sai_group_af[index] : 0U;
}

/* 四个传感器访问器：与 panel/sai 同一条纪律 —— 成套值、全成功才写 *out。
 * 返回 0 表示"DTB 里没有这颗器件或参数不合法"，消费方据此直接失败，
 * 不去猜地址（猜错的现场是"偶发 NACK"，比"节点缺失"难查得多）。 */
int board_rt_mpu9250(board_rt_mpu9250_t *out)
{
    parse_all();
    if (!s_rt.imu_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.imu;
    return 1;
}

int board_rt_ap3216c(board_rt_ap3216c_t *out)
{
    parse_all();
    if (!s_rt.als_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.als;
    return 1;
}

int board_rt_eeprom(board_rt_eeprom_t *out)
{
    parse_all();
    if (!s_rt.eeprom_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.eeprom;
    return 1;
}

int board_rt_tpad(board_rt_tpad_t *out)
{
    parse_all();
    if (!s_rt.tpad_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.tpad;
    return 1;
}

int board_rt_panel_ctrl(unsigned index, GPIO_TypeDef **port, unsigned *pin, uint32_t *af)
{
    parse_all();
    if (index >= BOARD_RT_LCD_PIN_COUNT || s_rt.panel_ctrl_port[index] == NULL) {
        return 0;
    }
    if (port != NULL) {
        *port = s_rt.panel_ctrl_port[index];
    }
    if (pin != NULL) {
        *pin = s_rt.panel_ctrl_pin[index];
    }
    if (af != NULL) {
        *af = s_rt.panel_ctrl_af[index];
    }
    return 1;
}

int board_rt_dac(board_rt_dac_t *out)
{
    parse_all();
    if (!s_rt.dac_ok || out == NULL) {
        return 0;
    }
    *out = s_rt.dac;
    return 1;
}
