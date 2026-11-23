#include "board_sdram.h"

#include "board_rt.h"
#include "bsl_device.h"
#include "bsl_device_api.h"
#include "bsl_gpio.h"

/* 引脚/时序/刷新全部来自 DTB（board_rt），这里不出现第二个 0xC0000000 或 730。
 * 寄存器值出处：
 *   SDCR/SDTR/模式寄存器值 — 参考工程 sdram.c 的 sdctrlreg / sdtimereg / mregval；
 *   刷新计数 — 本工程 HCLK=168 MHz（SDCLK=84 MHz）重算，见 generate.py 的节点注释。
 * 两者不是同一块板的同一频率，所以 DTB 里存的是**本工程的值**，不是抄参考工程的数。 */

#define SDRAM_PAGE_STEP 4096U
#define SDRAM_PATTERN 0xA5A5A5A5U

/* FMC SDRAM 命令寄存器：SDCMR[2:0]=命令，bit4=CTB1（Bank5），
 * [8:5]=自刷新次数，[20:9]=模式寄存器值。 */
#define SDRAM_CMD_CLK_EN 1U
#define SDRAM_CMD_PRECHARGE 2U
#define SDRAM_CMD_AUTOREFRESH 3U
#define SDRAM_CMD_LOAD_MODE 4U

/* 模式寄存器：突发长度 1、顺序、CAS=3、标准模式、单点写。值为 0x231。 */
#define SDRAM_MODE_REG 0x231U

static int s_ready;
static uintptr_t s_base;
static uint32_t s_size;

static int sdram_cmd(uint32_t cmd, uint32_t refresh, uint32_t mode)
{
    uint32_t retry;

    FMC_Bank5_6->SDCMR = (cmd & 0x7U) | (1UL << 4)
                         | ((refresh & 0xFU) << 5U) | ((mode & 0x1FFFU) << 9U);
    for (retry = 0U; retry < 0x200000UL; retry++) {
        if ((FMC_Bank5_6->SDSR & (1UL << 5)) == 0U) {
            return 0;
        }
    }
    return -1;
}

bsl_err_t board_sdram_init(void)
{
    uintptr_t base = 0U;
    uint32_t size = 0U;
    uint32_t sdcr = 0U;
    uint32_t sdtr = 0U;
    uint32_t refresh = 0U;
    unsigned groups;
    unsigned i;

    if (board_rt_sdram(&base, &size, &sdcr, &sdtr, &refresh) == 0) {
        return BSL_ERR_INVAL;
    }
    if (size == 0U) {
        return BSL_ERR_INVAL;
    }
    groups = board_rt_sdram_pin_groups();
    if (groups == 0U) {
        return BSL_ERR_INVAL;
    }
    /* 全组成功才继续：任一端口配不上，SDRAM 的读回会整片错，且没有任何读数
     * 能指出是哪个端口漏了。bsl_gpio_init_af 内部已开端口时钟。 */
    for (i = 0U; i < groups; i++) {
        if (bsl_gpio_init_af(board_rt_sdram_pin_port(i),
                             board_rt_sdram_pin_mask(i),
                             board_rt_sdram_pin_af(i),
                             GPIO_PULLUP,
                             GPIO_SPEED_FREQ_VERY_HIGH) == 0) {
            return BSL_ERR_IO;
        }
    }

    RCC->AHB3ENR |= RCC_AHB3ENR_FMCEN;
    (void)RCC->AHB3ENR; /* 读回一次，保证时钟已生效再写 FMC 寄存器 */

    FMC_Bank5_6->SDCR[0] = sdcr;
    FMC_Bank5_6->SDTR[0] = sdtr;

    if (sdram_cmd(SDRAM_CMD_CLK_EN, 0U, 0U) != 0) {
        return BSL_ERR_TIMEOUT;
    }
    /* 手册要求时钟使能后 ≥500 µs 再做预充电；HAL_Delay(1) 给 1 ms，不引入忙等。 */
    HAL_Delay(1U);
    if (sdram_cmd(SDRAM_CMD_PRECHARGE, 0U, 0U) != 0) {
        return BSL_ERR_TIMEOUT;
    }
    if (sdram_cmd(SDRAM_CMD_AUTOREFRESH, 8U, 0U) != 0) {
        return BSL_ERR_TIMEOUT;
    }
    if (sdram_cmd(SDRAM_CMD_LOAD_MODE, 0U, SDRAM_MODE_REG) != 0) {
        return BSL_ERR_TIMEOUT;
    }

    FMC_Bank5_6->SDRTR = refresh << 1U;

    s_base = base;
    s_size = size;
    s_ready = 1;
    return BSL_OK;
}

void *board_sdram_base(void)
{
    return (s_ready != 0) ? (void *)s_base : NULL;
}

uint32_t board_sdram_size(void)
{
    return (s_ready != 0) ? s_size : 0U;
}

unsigned board_sdram_selftest(void)
{
    uint32_t offset;
    unsigned bad = 0U;
    volatile uint32_t *p;

    if (s_ready == 0) {
        return 0xFFFFFFFFU;
    }
    for (offset = 0U; offset < s_size; offset += SDRAM_PAGE_STEP) {
        p = (volatile uint32_t *)(s_base + offset);
        *p = offset ^ SDRAM_PATTERN;
    }
    for (offset = 0U; offset < s_size; offset += SDRAM_PAGE_STEP) {
        p = (volatile uint32_t *)(s_base + offset);
        if (*p != (offset ^ SDRAM_PATTERN)) {
            bad++;
        }
    }
    return bad;
}

const bsl_sdram_api_t bsl_api_brd_fmc_sdram = {
    .base = board_sdram_base,
    .size = board_sdram_size,
};

const bsl_device_driver_t bsl_driver_brd_fmc_sdram = {
    .init = board_sdram_init,
};
