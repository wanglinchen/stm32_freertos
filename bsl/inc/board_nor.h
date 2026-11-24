#ifndef BOARD_NOR_H
#define BOARD_NOR_H

#include "bsl_err.h"

#include <stdint.h>

/* SPI NOR（W25Q256，32 MB）。引脚/容量/页/扇区全部来自 DTB 的 nor 节点，
 * 代码里不出现第二个 "PF6"、第二个 32 MB。
 *
 * 读一律用 0x13（Read Data with 4-Byte Address）：32 MB 器件用 3 字节地址读
 * 只能覆盖前 16 MB，越过边界的读不是失败、而是"静默回绕到 0" —— 现场表现是
 * "文件系统挂上去了，但后半段的文件内容全是文件头"。这与 0x90/0x9F 这类
 * 探测器件的命令无关，不需要先切 4 字节模式。 */

#define BOARD_NOR_JEDEC_LEN 3U

bsl_err_t board_nor_init(void);

/* 上一次读到的 JEDEC ID（3 字节，W25Q256 = 0xEF4019）。未初始化返回 0。 */
uint32_t board_nor_jedec(void);

/* 器件容量（字节）；未初始化返回 0。 */
uint32_t board_nor_size(void);

/* 从 addr 起读 len 字节（逐页 4 字节地址读命令，实现在 bsl 侧）。 */
bsl_err_t board_nor_read(uint32_t addr, void *buf, uint32_t len);

#endif /* BOARD_NOR_H */
