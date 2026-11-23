#ifndef BSL_DEVICE_API_H
#define BSL_DEVICE_API_H

#include "board_beep.h"
#include "board_debug_uart.h"
#include "board_key.h"
#include "board_led.h"

/* 各 compatible 在运行期的 ops。
 *
 * 这些结构里**没有** init：初始化只有 `bsl_device_driver_t.init` 一个入口，
 * 由 bsl_init_run() 按 init_level 调用。早前每个 api 结构都带一个
 * `void (*init)(void)`，与 driver 的 init 指向同一个函数，但全仓没有任何代码
 * 通过 api 调用它 —— 于是"改 api 里的 .init 以为改了初始化"这类静默无效
 * 有了可乘之机（与 DTS 里 gpios flags / pressed-* 双写同型）。已删除。
 *
 * 原先的 `bsl_debug_uart_api_t` / `bsl_api_debug_uart` 也已删除：`puts` 全仓零
 * 消费者（`bsl_log` 直接调 `board_debug_uart_puts`），留着只是一套"看起来能
 * 走 api 打日志"的面子接口。
 *
 * 类型与符号的对应关系不写在代码里：binding 用 `api_type` / `api_sym` 声明，
 * 生成期产出 `bsl_devapi_<node>()` 访问器（board_device_registry.h）。 */

typedef struct {
    void (*set)(board_led_id_t id, int on);
    void (*on)(board_led_id_t id);
    void (*off)(board_led_id_t id);
    void (*toggle)(board_led_id_t id);
} bsl_gpio_leds_api_t;

typedef struct {
    int (*pressed)(board_key_id_t id);
    int (*pin_high)(board_key_id_t id);
} bsl_gpio_keys_api_t;

typedef struct {
    void (*set)(int on);
    void (*on)(void);
    void (*off)(void);
} bsl_beep_api_t;

#endif