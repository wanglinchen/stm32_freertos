#include "bsl_init.h"

#include "bsl_device.h"
#include "bsl_log.h"

bsl_err_t bsl_init_run(bsl_init_level_t level)
{
    unsigned i;
    unsigned n = bsl_device_count();
    bsl_err_t first_err = BSL_OK;

    for (i = 0U; i < n; i++) {
        const bsl_device_t *dev = bsl_device_at(i);
        bsl_err_t rc;

        if (dev == NULL || dev->driver == NULL) {
            continue;
        }
        if (dev->init_level != level) {
            continue;
        }
        if (dev->driver->init == NULL) {
            /* 没有 init 的设备（例如只提供 api 表）也算就绪。 */
            bsl_device_mark(dev->id, BSL_DEV_STATE_READY);
            continue;
        }
        rc = dev->driver->init();
        if (rc == BSL_OK) {
            bsl_device_mark(dev->id, BSL_DEV_STATE_READY);
            continue;
        }

        bsl_device_mark(dev->id, BSL_DEV_STATE_FAILED);
        /* 只有标了 optional: false 的设备才把失败带出去。可选设备的失败由各自
         * 的消费者降级（fml 跳过该设备），把它们也算进首错会让"某个可选外设
         * 没接"看起来和"必需外设坏了"一模一样 —— 这正是 first_err 早前成了
         * 死值之后最容易退回去的形态。 */
        if (dev->optional == 0U && first_err == BSL_OK) {
            first_err = rc;
        }
        /* 失败必须留痕（早前 init 返回 void，这类失败完全不可见）。
         * 但只能在 UART 自己已经就绪时才打印：POST_CLOCK 级的第一个设备就是
         * debug_uart，它若初始化失败，此时写 UART 会把未初始化的 huart 交给
         * HAL_UART_Transmit（Instance 为 NULL → 解引用空指针）。所以先判就绪。 */
        if (bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
            BSL_LOG_MOD_ERR("bsl", "device init failed: ");
            bsl_log_puts(dev->name);
            BSL_LOG_ERR(" rc=");
            bsl_log_put_dec((int)rc);
            BSL_LOG_ERR("\r\n");
        }
    }
    return first_err;
}

bsl_err_t bsl_board_periph_init(void)
{
    bsl_err_t rc_post = bsl_init_run(BSL_INIT_LEVEL_POST_CLOCK);
    bsl_err_t rc_board = bsl_init_run(BSL_INIT_LEVEL_BOARD);
    bsl_err_t rc;

    /* 两级都要跑，不能"POST_CLOCK 失败就 return"：POST_CLOCK 只有一个
     * debug_uart，它一失败就提前返回，等于把 LED / 按键 / I2C / 蜂鸣器整组
     * 跳过 —— 串口一个故障被放大成"整机外设全缺"，fml 任务随即因为拿不到
     * api 而 panic(2)，只能靠看门狗反复复位。单级内部的失败不阻断同级其它
     * 设备（bsl_init_run 记下 first_err 后继续循环）。 */
    rc = (rc_post != BSL_OK) ? rc_post : rc_board;

    if (rc == BSL_OK) {
        return BSL_OK;
    }
    /* 串口可能正是失败的那个设备：此时写 UART 会把未初始化的 huart 交给
     * HAL_UART_Transmit（Instance 为 NULL → 解引用空指针，HardFault）。
     * 所以先判就绪再打。这一段留在 bsl 而不是 main：apl 一旦 include
     * bsl_device.h / bsl_log.h，apl → bsl 的层边就从 2 处变成 4 处。 */
    if (bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
        BSL_LOG_MOD_ERR("bsl", "required periph init failed rc=");
        bsl_log_put_dec((int)rc);
        BSL_LOG_ERR("\r\n");
    }
    return rc;
}
