#ifndef BOARD_NAND_H
#define BOARD_NAND_H

#include "bsl_device.h"
#include "bsl_device_api.h"

/* NAND（P9a 只读 / P9a-2 写通道）：FMC Bank3 上的 SK Hynix 512 MB 器件
 * （页 2048+64，块 64 页，4096 块；ID 读出来是 ad:dc:90:95:56，不按型号写死）。
 *
 * 通道边界：复位 / 读 ID / 读状态 / 读页 + **擦一个块 / 写一页 / 判坏块**。
 * 仍然**不做** FTL、坏块 LUT、硬件 ECC、FATFS 卷 1 —— 写通道只到"原料"这一层：
 * `erase_block` / `write_page` 返回 BSL_OK 只表示命令序列跑完且状态 I/O0=0，
 * **不代表数据可信**，调用方必须自己读回比对（POST 的 nand 项就是这么做的）。
 * 另外：原厂固件在这一片里放着系统文件，所以任何批量擦写都不许随手做；
 * POST 每轮只擦**一个**块（末块往前第一个"好且全 0xFF"的块），写完再擦回空白。 */
bsl_err_t board_nand_init(void);

extern const bsl_device_driver_t bsl_driver_brd_nand;
extern const bsl_nand_api_t bsl_api_brd_nand;

#endif /* BOARD_NAND_H */
