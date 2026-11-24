/*
 * 片内温度传感器（ADC1 通道 16）+ 出厂两点校准。
 *
 * 为什么这是"自检类"而不进设备枚举：唯一的消费者是本批的 POST 行。
 * 真实应用侧的温度读数需求目前不存在，注册成 BSL_DEV_x 只会多一个"注册了
 * 没人用"的面子接口（对照表 §1.4「注册门槛」原文）。
 *
 * 换算用芯片出厂写进系统存储区的两个标定点（TS_CAL1 30 °C / TS_CAL2 110 °C），
 * **不是**手册的典型曲线：曲线随批次离散，用典型值算出来的温度只能看个趋势。
 */
#ifndef BSL_ADC_H
#define BSL_ADC_H

#include <stdint.h>

#include "bsl_err.h"

typedef struct {
    uint16_t raw;        /* 通道 16 的 12 位原始码 */
    uint16_t cal30;      /* TS_CAL1（30 °C 处的码） */
    uint16_t cal110;     /* TS_CAL2（110 °C 处的码） */
    uint32_t vref_mv;    /* DTB 声明（0 = 未声明） */
    int32_t celsius_x10; /* 温度 ×10；cal_valid = 0 时无意义 */
    int cal_valid;       /* 1 = 标定点自洽且 VREF+ 在 3.3 V±0.1 V 内 */
} bsl_adc_temp_t;

/* 幂等初始化：开 ADC1 时钟（PCLK2/4 = 21 MHz，上限 36 MHz）、开温度传感器、
 * 采样时间 480 周期（手册要求内部温度通道 ≥ 10 µs，21 MHz 下 480 周期 = 22.9 µs）。 */
bsl_err_t bsl_adc_temp_init(void);

/* 采一次并换算。返回 BSL_ERR_NOINIT（没 init）/ TIMEOUT（EOC 没来）/ INVAL（标定点不可信）。 */
bsl_err_t bsl_adc_temp_read(bsl_adc_temp_t *out);

#endif /* BSL_ADC_H */
