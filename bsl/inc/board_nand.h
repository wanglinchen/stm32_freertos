#ifndef BOARD_NAND_H
#define BOARD_NAND_H

#include "bsl_device.h"
#include "bsl_device_api.h"

/* NAND（P9a）：FMC Bank3 上的 MT29F4G08ABADA（512 MB，页 2048+64）。
 * 本批只做只读通道（复位 / 读 ID / 读状态 / 读页），不擦不写 ——
 * 原厂固件在该片上放着系统文件，抹一块就可能毁数据；写通道要等 FTL
 * 把坏块与 ECC 一起接上，且 POST 得有"可判"的判据才有意义。 */
bsl_err_t board_nand_init(void);

extern const bsl_device_driver_t bsl_driver_brd_nand;
extern const bsl_nand_api_t bsl_api_brd_nand;

#endif /* BOARD_NAND_H */
