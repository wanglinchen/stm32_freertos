#include "bsl_device.h"

#include "bsl_err.h"

/* 扩展芯片无单独硬件配置；蜂鸣器驱动负责影子寄存器。
 * 原先整个驱动包在 #if BOARD_DTS_BEEP_VIA_PCF8574 里（生成宏），
 * 现在板子固定走 PCF8574，条件编译随生成头一起删除。 */
static bsl_err_t bsl_pcf8574_init(void)
{
    return BSL_OK;
}

const bsl_device_driver_t bsl_driver_nxp_pcf8574 = {
    .init = bsl_pcf8574_init,
};
