#include "board_rt.h"

#include "bsl_fdt.h"

#define RT_MAX_ITEMS 8U

#define RT_COMPAT_CLOCKS "brd,board-clocks"
#define RT_COMPAT_LEDS "brd,gpio-leds"
#define RT_COMPAT_KEYS "brd,gpio-keys"
#define RT_COMPAT_I2C "brd,i2c-gpio"
#define RT_COMPAT_BEEP "brd,pcf8574-beep"
#define RT_COMPAT_UART "brd,usart-ch340"

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
    parse_leds();
    parse_keys();
    parse_i2c();
    parse_beep();
    parse_uart();
}

uint32_t board_rt_hse_hz(void)
{
    parse_all();
    return s_rt.hse_hz;
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
