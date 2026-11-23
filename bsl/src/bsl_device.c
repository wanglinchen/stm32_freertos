#include "bsl_device.h"

#include <stddef.h>
#include <stdint.h>

#define BOARD_DEVICE_REGISTRY_ENTRY(dev_id, dev_name, level, driver_sym, optional) \
    {                                                                                \
        (dev_id),                                                                    \
        (dev_name),                                                                  \
        (level),                                                                     \
        &(driver_sym),                                                               \
        (optional),                                                                  \
    },

static const bsl_device_t s_devices[] = {
    BOARD_DEVICE_REGISTRY_APPLY(BOARD_DEVICE_REGISTRY_ENTRY)
};

/* 真实条数以数组本身为准，不用 BSL_DEV_COUNT。
 *
 * 历史缺陷：三个访问函数原来拿 `BSL_DEV_COUNT`（枚举里数了多少个 id）当数组长度：
 * 注册表条数少于枚举数时（某设备在 DTS/registry 里没有条目，生成器就少发一条
 * M(...)），`bsl_device_at(BSL_DEV_COUNT - 1)` 会取到数组末尾之外的一块内存，
 * bsl_init_run() 随即读它的 init_level / driver 并调用 —— 没有任何编译期或运行期
 * 报错，表现是启动期随机跳飞。反过来条数多于枚举数时按 id 查找会静默把第 id 项
 * 当成目标设备（顺序与枚举一旦不一致就取错）。
 * 所以：条数取 sizeof，按 id 的查找一律走 `device_index()` 线性扫 `.id`。
 */
#define BSL_DEVICE_TABLE_COUNT ((unsigned)(sizeof(s_devices) / sizeof(s_devices[0])))

/* 运行期判 NULL 只是兜底（bsl_init_run / 各 fml 消费方都判了），真正的契约在这里：
 * 注册表 M(...) 的条数必须与 bsl_dev_id_t 枚举项数一一对应。少了任何一条，
 * 那个设备在镜子里就不存在，bsl_device_state(BSL_DEV_x) 只会报 FAILED，
 * 而调用方通常把它解释成"设备可选"，于是启动照旧、功能静默缺失。
 * 生成器目前把枚举项和 M(...) 放在同一个循环里产出（恒相等），这条断言守的是
 * 生成物被手改、或生成链被改成两处来源这两种情形。 */
_Static_assert(
    (unsigned)BSL_DEV_COUNT == (unsigned)(sizeof(s_devices) / sizeof(s_devices[0])),
    "BOARD_DEVICE_REGISTRY_APPLY must emit exactly BSL_DEV_COUNT entries");

/* 运行态与设备表分离：s_devices 是 const（落 .rodata），state 必须可写。
 * 用 uint8_t 存枚举值（6 个设备只占 6 B）；用 enum 数组会按 4 B 对齐白占 24 B。
 * 初值 0 == BSL_DEV_STATE_UNINIT。 */
static uint8_t s_state[BSL_DEVICE_TABLE_COUNT];

static int device_index(bsl_dev_id_t id)
{
    unsigned i;

    for (i = 0U; i < BSL_DEVICE_TABLE_COUNT; i++) {
        if (s_devices[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

unsigned bsl_device_count(void)
{
    return BSL_DEVICE_TABLE_COUNT;
}

const bsl_device_t *bsl_device_at(unsigned index)
{
    if (index >= BSL_DEVICE_TABLE_COUNT) {
        return NULL;
    }
    return &s_devices[index];
}

bsl_dev_state_t bsl_device_state(bsl_dev_id_t id)
{
    int index = device_index(id);

    /* 表里没有这个 id，等价于"这个设备不可用"——不让调用方把"没有条目"
     * 误读成"可选设备、可以不初始化"。 */
    if (index < 0) {
        return BSL_DEV_STATE_FAILED;
    }
    return (bsl_dev_state_t)s_state[index];
}

int bsl_device_is_ready(bsl_dev_id_t id)
{
    return (bsl_device_state(id) == BSL_DEV_STATE_READY) ? 1 : 0;
}

void bsl_device_mark(bsl_dev_id_t id, bsl_dev_state_t state)
{
    int index = device_index(id);

    if (index < 0) {
        return;
    }
    s_state[index] = (uint8_t)state;
}
