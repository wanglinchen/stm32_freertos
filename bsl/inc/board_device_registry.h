/* 手写，勿删条目。设备实例注册表不再由构建期生成：
 * 它只说明「哪些 compatible 有对应 driver、按什么级别/顺序初始化」，
 * 设备事实（端口/引脚/地址）由 board_rt 在运行期从 DTB 读。
 *
 * 新增设备：在 bsl_dev_id_t 里加枚举、在 BOARD_DEVICE_REGISTRY_APPLY 里加一行，
 * 并保证 BSL_DEV_COUNT 与行数一致（bsl_device.c 有 _Static_assert 守着）。 */
#ifndef BOARD_DEVICE_REGISTRY_H
#define BOARD_DEVICE_REGISTRY_H

#include "bsl_init.h"
#include "bsl_device_api.h"

typedef enum {
    BSL_DEV_DEBUG_UART = 0,
    BSL_DEV_SDRAM,
    BSL_DEV_PANEL,
    BSL_DEV_TOUCH,
    BSL_DEV_LEDS,
    BSL_DEV_I2C_GPIO,
    BSL_DEV_KEYS,
    BSL_DEV_PCF8574,
    BSL_DEV_BEEP,
    BSL_DEV_COUNT
} bsl_dev_id_t;

/* 节点 debug_serial: compatible = "brd,usart-ch340" → bsl_driver_debug_uart */
/* 节点 sdram: compatible = "brd,fmc-sdram" → bsl_driver_brd_fmc_sdram */
/* 节点 panel: compatible = "brd,rgb-panel" → bsl_driver_brd_rgb_panel */
/* 节点 touch: compatible = "goodix,gt9147" → bsl_driver_goodix_gt9147 */
/* 节点 leds: compatible = "brd,gpio-leds" → bsl_driver_brd_gpio_leds */
/* 节点 i2c: compatible = "brd,i2c-gpio" → bsl_driver_brd_i2c_gpio */
/* 节点 keys: compatible = "brd,gpio-keys" → bsl_driver_brd_gpio_keys */
/* 节点 pcf8574: compatible = "nxp,pcf8574" → bsl_driver_nxp_pcf8574 */
/* 节点 beep: compatible = "brd,pcf8574-beep" → bsl_driver_brd_pcf8574_beep */

struct bsl_device_driver;
extern const struct bsl_device_driver bsl_driver_brd_gpio_keys;
extern const struct bsl_device_driver bsl_driver_brd_gpio_leds;
extern const struct bsl_device_driver bsl_driver_brd_i2c_gpio;
extern const struct bsl_device_driver bsl_driver_brd_pcf8574_beep;
extern const struct bsl_device_driver bsl_driver_brd_fmc_sdram;
extern const struct bsl_device_driver bsl_driver_brd_rgb_panel;
extern const struct bsl_device_driver bsl_driver_goodix_gt9147;
extern const struct bsl_device_driver bsl_driver_debug_uart;
extern const struct bsl_device_driver bsl_driver_nxp_pcf8574;

extern const bsl_gpio_leds_api_t bsl_api_brd_gpio_leds;
extern const bsl_gpio_keys_api_t bsl_api_brd_gpio_keys;
extern const bsl_beep_api_t bsl_api_brd_beep;
extern const bsl_sdram_api_t bsl_api_brd_fmc_sdram;
extern const bsl_panel_api_t bsl_api_brd_rgb_panel;
extern const bsl_touch_api_t bsl_api_goodix_gt9147;

/* BSL_DEV_LEDS: brd,gpio-leds 的 api，类型在编译期确定 */
static inline const bsl_gpio_leds_api_t *bsl_devapi_leds(void)
{
    return &bsl_api_brd_gpio_leds;
}

/* BSL_DEV_KEYS: brd,gpio-keys 的 api，类型在编译期确定 */
static inline const bsl_gpio_keys_api_t *bsl_devapi_keys(void)
{
    return &bsl_api_brd_gpio_keys;
}

/* BSL_DEV_BEEP: brd,pcf8574-beep 的 api，类型在编译期确定 */
static inline const bsl_beep_api_t *bsl_devapi_beep(void)
{
    return &bsl_api_brd_beep;
}

/* BSL_DEV_SDRAM: brd,fmc-sdram 的 api，类型在编译期确定 */
static inline const bsl_sdram_api_t *bsl_devapi_sdram(void)
{
    return &bsl_api_brd_fmc_sdram;
}

/* BSL_DEV_PANEL: brd,rgb-panel 的 api，类型在编译期确定 */
static inline const bsl_panel_api_t *bsl_devapi_panel(void)
{
    return &bsl_api_brd_rgb_panel;
}

/* BSL_DEV_TOUCH: goodix,gt9147 的 api，类型在编译期确定 */
static inline const bsl_touch_api_t *bsl_devapi_touch(void)
{
    return &bsl_api_goodix_gt9147;
}

#define BOARD_DEVICE_REGISTRY_APPLY(M) \
    M(BSL_DEV_DEBUG_UART, "debug_uart", BSL_INIT_LEVEL_POST_CLOCK, bsl_driver_debug_uart, 0) \
    M(BSL_DEV_SDRAM, "sdram", BSL_INIT_LEVEL_BOARD, bsl_driver_brd_fmc_sdram, 0) \
    M(BSL_DEV_PANEL, "panel", BSL_INIT_LEVEL_BOARD, bsl_driver_brd_rgb_panel, 0) \
    M(BSL_DEV_TOUCH, "touch", BSL_INIT_LEVEL_BOARD, bsl_driver_goodix_gt9147, 0) \
    M(BSL_DEV_LEDS, "leds", BSL_INIT_LEVEL_BOARD, bsl_driver_brd_gpio_leds, 1) \
    M(BSL_DEV_I2C_GPIO, "i2c_gpio", BSL_INIT_LEVEL_BOARD, bsl_driver_brd_i2c_gpio, 1) \
    M(BSL_DEV_KEYS, "keys", BSL_INIT_LEVEL_BOARD, bsl_driver_brd_gpio_keys, 1) \
    M(BSL_DEV_PCF8574, "pcf8574", BSL_INIT_LEVEL_BOARD, bsl_driver_nxp_pcf8574, 1) \
    M(BSL_DEV_BEEP, "beep", BSL_INIT_LEVEL_BOARD, bsl_driver_brd_pcf8574_beep, 1)

#endif /* BOARD_DEVICE_REGISTRY_H */
