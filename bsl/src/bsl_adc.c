#include "bsl_adc.h"

#include "board_rt.h"
#include "stm32f4xx_hal.h"

/* 出厂校准值放在系统存储区（RM0090 §30.3 / 表 "Temperature sensor calibration"）：
 *   0x1FFF7A2C  TS_CAL1 @ 30 °C，0x1FFF7A2E  TS_CAL2 @ 110 °C。
 * 只读区，不需要任何时钟。 */
#define ADC_TS_CAL1 (*(const uint16_t *)0x1FFF7A2CU)
#define ADC_TS_CAL2 (*(const uint16_t *)0x1FFF7A2EU)

#define ADC_TEMP_CHANNEL 16U
/* SMPRx 里 3 bit 的编码，0b110 = 480 周期 */
#define ADC_TEMP_SMP 6U
#define ADC_TEMP_SMP_SHIFT 16U /* SMPR1 里通道 16 占 [18:16] */

#define ADC_EOC_TIMEOUT_MS 5U

/* VREF+ 判据：校准值只在 3.3 V ±0.1 V 下成立；没声明（vref_mv = 0）时不否决，
 * 因为这颗板子的 VREF+ 就来自 3.3 V 电源，缺省是"没写"而不是"不是 3.3 V"。 */
#define ADC_VREF_MIN_MV 3200U
#define ADC_VREF_MAX_MV 3400U

static int s_inited;

bsl_err_t bsl_adc_temp_init(void)
{
    if (s_inited) {
        return BSL_OK;
    }
    __HAL_RCC_ADC1_CLK_ENABLE();

    /* ADCPRE：PCLK2 = 84 MHz，ADCCLK 上限 36 MHz → /2 是 42 MHz，超限；必须 /4。 */
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;
    /* TSVREFE 是温度传感器与 VREFINT 的总开关，不置位时通道 16 读到的是噪声 */
    ADC->CCR |= ADC_CCR_TSVREFE;

    ADC1->CR1 = 0U; /* 12 位、单次、非扫描 */
    ADC1->CR2 = 0U; /* 右对齐、软件触发 */
    ADC1->SMPR1 = (ADC1->SMPR1 & ~(0x7UL << ADC_TEMP_SMP_SHIFT))
                | ((uint32_t)ADC_TEMP_SMP << ADC_TEMP_SMP_SHIFT);
    ADC1->SMPR2 = 0U;
    ADC1->SQR1 = 0U; /* L = 0：规则序列只有 1 个转换 */
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = ADC_TEMP_CHANNEL;

    ADC1->CR2 = ADC_CR2_ADON;
    /* ADON 之后温度传感器要 tSTAB（约 10 µs）才稳定。480 周期采样本身已覆盖
     * 稳态，但首次转换前留一拍更保险；不做第二次 ADON 的"唤醒"花样。 */
    HAL_Delay(1U);

    s_inited = 1;
    return BSL_OK;
}

bsl_err_t bsl_adc_temp_read(bsl_adc_temp_t *out)
{
    board_rt_adc_t vref;
    uint32_t start;
    uint16_t raw;
    uint16_t cal30;
    uint16_t cal110;
    uint32_t vref_mv = 0U;

    if (!s_inited) {
        return BSL_ERR_NOINIT;
    }
    if (out == NULL) {
        return BSL_ERR_INVAL;
    }

    ADC1->CR2 |= ADC_CR2_SWSTART;
    start = HAL_GetTick();
    while ((ADC1->SR & ADC_SR_EOC) == 0U) {
        if ((HAL_GetTick() - start) > ADC_EOC_TIMEOUT_MS) {
            return BSL_ERR_TIMEOUT;
        }
    }
    /* 读 DR 同时清 EOC —— 下一次 SWSTART 之前必须读掉，否则第二次永远等不到 EOC */
    raw = (uint16_t)(ADC1->DR & 0x0FFFU);

    cal30 = ADC_TS_CAL1;
    cal110 = ADC_TS_CAL2;
    if (board_rt_adc_temp(&vref) != 0) {
        vref_mv = vref.vref_mv;
    }

    out->raw = raw;
    out->cal30 = cal30;
    out->cal110 = cal110;
    out->vref_mv = vref_mv;
    out->celsius_x10 = 0;

    /* 标定值自洽性：cal110 > cal30、两者都不是空片值 0/0xFFFF；
     * 电压在窗口内。任一不成立就只报原始码，不给出一个看着像温度的假数。 */
    if (cal110 > cal30 && cal30 != 0U && cal110 != 0xFFFFU
        && (vref_mv == 0U || (vref_mv >= ADC_VREF_MIN_MV && vref_mv <= ADC_VREF_MAX_MV))) {
        out->celsius_x10 = 300
                         + (int32_t)((int32_t)800 * ((int32_t)raw - (int32_t)cal30)
                                     / ((int32_t)cal110 - (int32_t)cal30));
        out->cal_valid = 1;
        return BSL_OK;
    }

    out->cal_valid = 0;
    return BSL_ERR_INVAL;
}
