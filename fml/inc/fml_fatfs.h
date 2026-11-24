#ifndef FML_FATFS_H
#define FML_FATFS_H

#include "bsl_err.h"

#include <stdint.h>

/* FatFs 挂在 NOR 上的卷 0（`pdrv` 0、逻辑盘 ""、逻辑扇区 512 B、擦除块 4 KB）。
 *
 * 逻辑扇区与擦除粒度说实话：卷的 BPB 写的是 512 B/扇区（板上已有卷的既有格式），
 * 而 NOR 只能按 4 KB 擦。两者由 diskio 的读改写连接，FatFs 这边看到的始终是 512。
 *
 * 单线程约束：本工程 `FF_FS_REENTRANT = 0`，FatFs 自己不加锁。当前只有一个
 * 消费者（POST + 未来的文件浏览应用），跨任务访问应由调用方串行化 —— 在
 * 没有第二个消费者之前不引入信号量，否则就是一条没人测的分支。
 * 一旦有第二个任务要读文件，先开 `FF_FS_REENTRANT = 1` 并提供 ff_mutex_*
 * 的 osl 实现，再放开。 */

#define FML_FATFS_VOL ""
#define FML_FATFS_NAME_LEN 13U

/* 一条根目录项。name 是 FatFs 的 8.3 短名原文（`GBK16   FON` 这样，
 * 8 字节名 + 3 字节扩展名各带填充空格，中间不含点）—— 不在这里拼成 "GBK16.FON"：
 * 短名里允许空格，拼接规则（哪些空格是填充、哪些是名字的一部分）属于展示层，
 * 由调用方自己决定怎么打。FF_USE_LFN = 0，所以长名不可用。 */
typedef struct {
    char name[FML_FATFS_NAME_LEN];
    uint32_t size;
    uint8_t is_dir;   /* 1 = 目录（size 为 0）。列目录时"条目数"里混着目录，
                       * 不标出来会被当成"有 N 个文件"。 */
} fml_fatfs_dirent_t;

/* 挂载卷 0（opt=1：立即挂载，不延迟到首次访问）。
 * 返回 BSL_OK = 挂上；BSL_ERR_INVAL = NOR 未就绪；BSL_ERR_IO = 挂载失败
 * （具体 FatFs 结果用 fml_fatfs_last_error() 看，常见 FR_NO_FILESYSTEM）。 */
bsl_err_t fml_fatfs_mount(void);

/* 1 = 已挂载。 */
int fml_fatfs_mounted(void);

/* 卷容量（KB）；未挂载返回 0。 */
uint32_t fml_fatfs_total_kb(void);

/* 列目录 path 的前 max 项，count_out 收到**全部**项数（可能大于 max，此时 buf
 * 只填前 max 项）。path 用 FatFs 的路径语法（"/"、"/SYSTEM"）。
 * 返回 BSL_OK / BSL_ERR_INVAL（未挂载或参数为 NULL）/ BSL_ERR_IO
 * （opendir/readdir 失败，码在 last_error）。
 *
 * 不叫 root_list：P3 的字库在子目录里（板上根目录只有一个 SYSTEM 目录），
 * 只读根目录等于永远读不到字库。 */
bsl_err_t fml_fatfs_list_dir(const char *path, fml_fatfs_dirent_t *buf, unsigned max,
                             unsigned *count_out);

/* 上一次失败的 FatFs 结果码（FRESULT 原文），未失败返回 0。
 * 不在这里翻译成字符串：FRESULT 与 ff.h 一一对应，现场查手册比多一张表更可靠。 */
int fml_fatfs_last_error(void);

#endif /* FML_FATFS_H */
