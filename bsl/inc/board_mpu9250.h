/*
 * 板级器件：InvenSense MPU9250（三轴加速度 + 三轴陀螺 + 片内 AK8963 三轴磁力计）。
 *
 * 型号不是猜的：P6c-0 的 `i2c 68 r75=71` 读到的 `WHO_AM_I(0x75)=0x71` 是 MPU9250
 * 的固定值（MPU6050 是 0x68、MPU6500 是 0x70），所以 pinmap 里的 QMI8658A 是错的。
 *
 * 磁力计有两个容易踩的点，都写在 init 里：
 *   1) AK8963 不是常驻总线器件，只有先写 INT_PIN_CFG(0x37).BYPASS_EN=1 才出现 ——
 *      P6c-0 那次 `i2c 0c r00=nack` 是"旁路没开"的正常现象，不是板上没有磁力计；
 *   2) 磁力计的单次/连续转换要走它自己的 CNTL1(0x0A)，与 MPU 的量程配置无关。
 *
 * 本层只提供"初始化 + 读原始值"，不做姿态解算、不做 DMP、不做温补：
 * 那些属于应用（§1.3 的 gyro_play / grad_play），而且需要真的量程定标。
 */
#ifndef BOARD_MPU9250_H
#define BOARD_MPU9250_H

#include "bsl_err.h"

#include <stdint.h>

bsl_err_t board_mpu9250_init(void);
int board_mpu9250_ready(void);

/* `WHO_AM_I` 的实测值（正常应为 0x71）；未初始化时返回 0。 */
uint8_t board_mpu9250_who(void);
/* 磁力计 AK8963 的 `WIA`（正常应为 0x48）；DTB 没给 mag-addr 或未开旁路时返回 0。 */
uint8_t board_mpu9250_mag_wia(void);

/* 读加速度/陀螺的原始 16 位值（量程由 ACCEL_CONFIG/GYRO_CONFIG 决定，
 * 换算系数不在这里 —— 换量程就得换算，写死一份系数只会在改配置时静默出错）。
 * 指针允许为 NULL。 */
bsl_err_t board_mpu9250_read(int16_t acc[3], int16_t gyr[3]);

#endif
