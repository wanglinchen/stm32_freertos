#include "bsl_rng.h"

#include "stm32f4xx_hal.h"

#define RNG_TIMEOUT_MS 5U

static int s_inited;

bsl_err_t bsl_rng_init(void)
{
    if (s_inited) {
        return BSL_OK;
    }
    __HAL_RCC_RNG_CLK_ENABLE();
    RNG->CR |= RNG_CR_RNGEN;
    s_inited = 1;
    return BSL_OK;
}

bsl_err_t bsl_rng_read(uint32_t *out)
{
    uint32_t start;

    if (!s_inited) {
        return BSL_ERR_NOINIT;
    }
    if (out == NULL) {
        return BSL_ERR_INVAL;
    }

    start = HAL_GetTick();
    while ((RNG->SR & RNG_SR_DRDY) == 0U) {
        /* 种子错误时必须主动清理：SECS 是粘滞位，留着会让 DRDY 再也不置起 */
        if ((RNG->SR & RNG_SR_SECS) != 0U) {
            RNG->SR = 0U;
            return BSL_ERR_IO;
        }
        if ((HAL_GetTick() - start) > RNG_TIMEOUT_MS) {
            return BSL_ERR_TIMEOUT;
        }
    }
    *out = RNG->DR; /* 读 DR 清 DRDY */
    return BSL_OK;
}
