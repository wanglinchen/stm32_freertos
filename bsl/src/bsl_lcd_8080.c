#include "bsl_lcd_8080.h"

#include "board_rt.h"
#include "bsl_gpio.h"
#include "stm32f4xx_hal.h"

/*
 * FMC Bank1 模式 A（异步 SRAM/PSRAM 型），16 位总线。
 *
 * 时序值不是算出来的，是跟原厂固件在板上对拍出来的（[实测] 2026-11-25）：
 *   BCR1  = 0x00005011 → MBKEN + MWID=16 位 + WREN + EXTMOD
 *   BTR1  = 0x0000460F → ADDSET=15、DATAST=70（读/写共同基础段）
 *   BWTR1 = 0x00000202 → ADDSET=10、DATAST=10（EXTMOD 下写周期用它）
 * 读走 BTR（慢，只有读 ID/读 GRAM 用），写走 BWTR（快，像素刷屏走这条）。
 * 168 MHz 下 1 HCLK = 5.95 ns：写一像素约 (10+10+1) * 5.95 ≈ 125 ns。
 */
#define LCD8080_BCR (FMC_BCR1_MBKEN | FMC_BCR1_MWID_0 | FMC_BCR1_WREN | FMC_BCR1_EXTMOD)
#define LCD8080_BTR (15UL | (70UL << 8))
#define LCD8080_BWTR (10UL | (10UL << 8))

static int s_ready;

int bsl_lcd_8080_ready(void)
{
    return s_ready;
}

bsl_err_t bsl_lcd_8080_init(void)
{
    GPIO_TypeDef *port;
    unsigned pin;
    uint32_t af;
    unsigned groups;
    unsigned i;

    s_ready = 0;

    /* 控制脚 CS/RS/NWE/NOE：DTB 里各一条 3-cell 属性，端口由 phandle 决定。 */
    for (i = 0U; i < BOARD_RT_LCD_PIN_COUNT; i++) {
        if (board_rt_panel_ctrl(i, &port, &pin, &af) == 0) {
            return BSL_ERR_INVAL;
        }
        if (bsl_gpio_init_af(port, (uint16_t)(1UL << pin), af,
                             GPIO_NOPULL, GPIO_SPEED_FREQ_VERY_HIGH) == 0) {
            return BSL_ERR_IO;
        }
    }

    /* 数据脚：PD 与 PE 两组，16 位分散在这两个端口的不同位（见 DTB lcd-data-pins）。 */
    groups = board_rt_panel_pin_groups();
    if (groups == 0U) {
        return BSL_ERR_INVAL;
    }
    for (i = 0U; i < groups; i++) {
        port = board_rt_panel_pin_port(i);
        if (port == NULL) {
            return BSL_ERR_INVAL;
        }
        if (bsl_gpio_init_af(port, board_rt_panel_pin_mask(i),
                             board_rt_panel_pin_af(i),
                             GPIO_NOPULL, GPIO_SPEED_FREQ_VERY_HIGH) == 0) {
            return BSL_ERR_IO;
        }
    }

    RCC->AHB3ENR |= RCC_AHB3ENR_FMCEN;
    (void)RCC->AHB3ENR; /* 读回一次，保证 FMC 时钟已生效再写它的寄存器 */

    FMC_Bank1->BTCR[0] = LCD8080_BCR;
    FMC_Bank1->BTCR[1] = LCD8080_BTR;
    FMC_Bank1E->BWTR[0] = LCD8080_BWTR;

    /* 厂商注释：FMC 配好之后必须等一会才能开始对面板发命令（原文 lcd_opt_delay）。
     * 这里用 1 ms，不引入忙等。 */
    HAL_Delay(1U);

    /* 读回校验：BCR 的 SRAM 型/16 位/EXTMOD 位与两份时序必须原样落进去。 */
    if ((FMC_Bank1->BTCR[0] & (FMC_BCR1_MBKEN | FMC_BCR1_MWID | FMC_BCR1_WREN | FMC_BCR1_EXTMOD))
            != (LCD8080_BCR & (FMC_BCR1_MBKEN | FMC_BCR1_MWID | FMC_BCR1_WREN | FMC_BCR1_EXTMOD))) {
        return BSL_ERR_IO;
    }
    if (FMC_Bank1->BTCR[1] != LCD8080_BTR || FMC_Bank1E->BWTR[0] != LCD8080_BWTR) {
        return BSL_ERR_IO;
    }

    s_ready = 1;
    return BSL_OK;
}

void bsl_lcd_8080_cmd(uint16_t cmd)
{
    if (s_ready == 0) {
        return;
    }
    *BSL_LCD_8080_CMD_ADDR = cmd;
}

void bsl_lcd_8080_data(uint16_t value)
{
    if (s_ready == 0) {
        return;
    }
    *BSL_LCD_8080_DAT_ADDR = value;
}

void bsl_lcd_8080_data_burst(const uint16_t *values, unsigned count)
{
    volatile uint16_t *dat = BSL_LCD_8080_DAT_ADDR;
    unsigned i;

    if (s_ready == 0 || values == NULL) {
        return;
    }
    for (i = 0U; i < count; i++) {
        *dat = values[i];
    }
}

uint16_t bsl_lcd_8080_read(void)
{
    if (s_ready == 0) {
        return 0U;
    }
    return *BSL_LCD_8080_DAT_ADDR;
}
