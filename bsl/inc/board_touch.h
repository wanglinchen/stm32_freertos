#ifndef BOARD_TOUCH_H
#define BOARD_TOUCH_H

#include <stdint.h>

#include "bsl_err.h"

/*
 * GT9xxx 电容触摸（4.3 寸屏自带），走 PH6/PI3 上的**软件 I2C**。
 *
 * 与其它 board_* 同构：init 返回 bsl_err_t，由 bsl_init_run() 写成 READY/FAILED；
 * 操作面只有 read()，消费方先看 bsl_device_is_ready(BSL_DEV_TOUCH)。
 *
 * 读点的关键在状态寄存器 0x814E：bit7 = 有新数据，低 4 位 = 点数。
 * **读完后必须写 0 清掉**，否则 IC 认为这一帧还没被取走，下一帧不再更新 ——
 * 现象是"第一次点有反应，之后坐标永远冻住"，极易被当成触摸 IC 坏了。
 */

bsl_err_t board_touch_init(void);

/* 总线错（NACK/ACK 超时）。与"无触点(0)"分开返回：前者是硬件/接线问题，
 * 后者是正常静止状态，混成一个 0 会让 POST 把断线判成"没按"。 */
#define BOARD_TOUCH_BUS_ERROR 0xFFFFFFFFU

/* 读当前触点：返回点数（0 = 无触点，BOARD_TOUCH_BUS_ERROR = 总线错）。
 * 坐标只在返回值落在 1..max_points 之间时有效，单位为面板像素（横屏、左上原点）。 */
unsigned board_touch_read(int32_t *x, int32_t *y);

/* 读到的产品 ID（"911"/"9147"/"1158"/"9271"）；未初始化返回 NULL。 */
const char *board_touch_product_id(void);

#endif
