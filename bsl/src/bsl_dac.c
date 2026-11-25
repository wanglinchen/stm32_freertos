#include "bsl_dac.h"

#include "board_rt.h"
#include "bsl_gpio.h"
#include "stm32f4xx_hal.h"

/* DOR1 是转换结果寄存器，不是 DHR 的影子：写 DHR → 转换 → DOR 更新需要时间。
 * 缓冲器开着时建立时间是 µs 量级（手册 tSETTLING 在缓冲关闭时给 3 µs），
 * 这里按 1 µs 量级留余量：168 MHz 下 200 次空转约 1.2 µs。 */
#define DAC_SETTLE_NOPS 200U

static int s_inited;

bsl_err_t bsl_dac_init(void)
{
    board_rt_dac_t cfg;

    if (s_inited) {
        return BSL_OK;
    }
    if (board_rt_dac(&cfg) == 0) {
        /* DTS 没挂 dac 节点：不猜 PA4，直接报不支持 */
        return BSL_ERR_NOSUPPORT;
    }
    if (cfg.channel != 1U) {
        /* OUT2 是 PA5（TPAD 的脚），驱动只做 OUT1 */
        return BSL_ERR_NOSUPPORT;
    }
    /* 模拟态：MODER=11 关掉输入/输出数字缓冲，DAC 输出直连引脚；
     * 同一根脚又接 ADC1_IN4，所以这一步同时是 ADC 侧的准备。
     *
     * 注意 cfg.pin 是**脚号**（4），bsl_gpio_init_* 要的是**脚位掩码**（1<<4）：
     * 直接传脚号会静默配到 PA2（掩码 0x0004），现象是"init 成功、MODER 里
     * 另一根脚变成模拟态"，比报错难查得多。 */
    if (cfg.pin >= 16U) {
        return BSL_ERR_INVAL;
    }
    if (bsl_gpio_init_analog(cfg.port, (uint16_t)(1U << cfg.pin)) == 0) {
        return BSL_ERR_IO;
    }

    __HAL_RCC_DAC_CLK_ENABLE();

    /* 全零重置再单独置 EN1：
     *   TEN1=0（软件触发，不挂定时器）
     *   TSEL1=0（触发源无关，TEN=0 时不生效）
     *   WAVE1=0（不用噪声/三角波发生器）
     *   DMAEN1=0（不走 DMA）
     *   BOFF1=0（**输出缓冲开**）—— 参考工程关缓冲（BOFF1=1），那样输出阻抗
     *     15 kΩ，低端/高端被负载拉偏、ADC 端也要等更久；本板这一路只服务自己的
     *     ADC 通道，开缓冲线性区更宽。
     */
    DAC->CR = 0U;
    DAC->DHR12R1 = 0U;
    DAC->CR = DAC_CR_EN1;

    s_inited = 1;
    return BSL_OK;
}

uint32_t bsl_dac_clock_hz(void)
{
    /* DAC 挂在 APB1；APB1 定时器时钟在这里不适用（DAC 不挂定时器），
     * 报的是总线时钟本身，POST 行打出来是为了"时钟树变了能看出来"。 */
    return HAL_RCC_GetPCLK1Freq();
}

bsl_err_t bsl_dac_set_code(uint16_t code)
{
    if (!s_inited) {
        return BSL_ERR_NOINIT;
    }
    if (code > BSL_DAC_CODE_MAX) {
        return BSL_ERR_INVAL;
    }
    DAC->DHR12R1 = (uint32_t)code;
    return BSL_OK;
}

uint16_t bsl_dac_code(void)
{
    volatile uint32_t n;

    if (!s_inited) {
        return 0U;
    }
    for (n = 0U; n < DAC_SETTLE_NOPS; n++) {
        __NOP();
    }
    return (uint16_t)(DAC->DOR1 & 0x0FFFU);
}
