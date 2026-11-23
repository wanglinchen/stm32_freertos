#ifndef BSL_DEVICE_H
#define BSL_DEVICE_H

#include <stddef.h>

#include "board_device_registry.h"
#include "bsl_err.h"
#include "bsl_init.h"

/* 设备运行态。初始化失败必须留下状态位。
 *
 * 历史缺陷：driver->init 早前是 `void (*)(void)`，没有返回通道。board_beep 在
 * I2C 总线没起来时照样"初始化成功"进入运行态（pcf8574_write 的 BSL_ERR_IO 被
 * beep_apply_shadow() 丢弃），现场表现只是"蜂鸣器坏了"，卡上没有任何可查的
 * 状态；那条 ERROR 日志也可能被串口时序或日志等级吞掉。
 * 现在：init 返回 bsl_err_t，bsl_init_run() 据此写 state，消费方用
 * bsl_device_is_ready() 判断，不必猜。
 */
typedef enum {
    BSL_DEV_STATE_UNINIT = 0, /* 还没跑到它的 init_level */
    BSL_DEV_STATE_READY,      /* driver->init 返回 BSL_OK */
    BSL_DEV_STATE_FAILED      /* driver->init 返回非 BSL_OK（或该 id 不在表里） */
} bsl_dev_state_t;

/* 驱动只有"初始化"这一件事。设备的 api（leds/keys/beep 的 ops）不再挂在这里：
 * 早前是 `const void *api`，消费方靠 `(const bsl_gpio_leds_api_t *)dev->driver->api`
 * 强转回来 —— 结构类型在编译期丢失，任何类型不匹配都只在运行期按错布局取函数指针。
 * 现在 api 的"类型 + 符号"写在 binding 的 `api_type` / `api_sym`，生成期产出
 * `bsl_devapi_<node>()` 类型化访问器（board_device_registry.h），消费方直接调用。 */
typedef struct bsl_device_driver {
    bsl_err_t (*init)(void);
} bsl_device_driver_t;

typedef struct bsl_device {
    bsl_dev_id_t id;
    const char *name;
    bsl_init_level_t init_level;
    const bsl_device_driver_t *driver;
    /* 1 = 缺失时允许降级运行（消费者跳过该设备）；0 = 必需。
     * 来自 binding 的 `optional:`，由生成器写进 `M(...)` 的第 5 个参数。
     * C 里不设默认值：`optional` 缺省就意味着"没人想过这个设备缺失时该不该
     * 继续跑"，而那正是这个字段要消灭的形态（早前只写在 fml_button.c 的 if）。 */
    unsigned optional;
} bsl_device_t;

/* 遍历用 at（下标）+ count；按 id 取运行态用 state / is_ready。
 * 曾经还有 `bsl_device_get(id)` 返回记录指针：全仓零调用，被 `--gc-sections` 丢弃，
 * 而头里还留着声明 —— 于是「这个 API 在镜像里」是假的，`nm` 查不到任何符号。
 * 第八轮删除（与第六轮删四个零调用 API 同口径：声明必须在镜像里有对应符号）。 */
const bsl_device_t *bsl_device_at(unsigned index);
unsigned bsl_device_count(void);

/* 运行态：bsl_init_run() 写，fml/其它消费方读。查不到该 id 时按 FAILED 处理。 */
bsl_dev_state_t bsl_device_state(bsl_dev_id_t id);
int bsl_device_is_ready(bsl_dev_id_t id);
void bsl_device_mark(bsl_dev_id_t id, bsl_dev_state_t state);

#endif
