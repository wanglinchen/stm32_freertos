#include "bsl_crc.h"

#include "stm32f4xx_hal.h"

uint32_t bsl_crc32_words(const uint32_t *words, unsigned count)
{
    unsigned i;

    if (words == NULL || count == 0U) {
        return 0U;
    }

    /* 不置 CRCEN 也能写 DR，但写进去的值不会进移位寄存器 —— 读回来恒是复位值。
     * 这种"读到常量"的失败在 POST 里表现为"结果不对"，看不出是时钟没开。 */
    __HAL_RCC_CRC_CLK_ENABLE();

    /* CR.RESET 把 DR 置回 init 值 0xFFFFFFFF，等价于重新开始一次计算。
     * 不复位就复用单元的话，第二次调用的结果取决于第一次算了什么 —— 那是
     * "看起来能用、只在连续调用时错"的形态。 */
    CRC->CR = CRC_CR_RESET;

    for (i = 0U; i < count; i++) {
        CRC->DR = words[i];
    }
    return CRC->DR;
}
