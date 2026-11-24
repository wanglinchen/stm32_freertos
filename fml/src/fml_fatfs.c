#include "fml_fatfs.h"

#include <string.h>

#include "board_device_registry.h"
#include "board_nor.h"
#include "bsl_device.h"
#include "bsl_device_api.h"

/* ff.h 必须在 diskio.h 之前：diskio.h 用的是 ff.h 里的 BYTE/UINT/LBA_t，
 * 自己不带类型定义，反过来包含会得到一串 "unknown type name 'BYTE'"
 * —— 而下面的 disk_* 函数是靠 diskio.h 里的原型消掉 -Wmissing-prototypes 的。 */
#include "ff.h"
#include "diskio.h"

/* 卷 0 = 板载 NOR。FatFs 的 physical drive 号只在本文件里出现一次：
 * 多卷（SD/NAND）进来时在这里加 pdrv 分支，不要让 ff.h 的 FF_VOLUMES 与
 * 这里的 if 各自记一份。
 *
 * 两个粒度必须分清：
 *   FATFS_SECTOR —— **逻辑**扇区，512 B，是板上卷 BPB 写的值，FatFs 按它发 LBA；
 *   FATFS_ERASE  —— **物理**擦除块，4 KB，W25Q256 的扇区擦单位。
 * 两者之间只差一次读改写（见 disk_write）：写 512 B 时必须先把所在 4 KB 整块
 * 读回来，否则擦完那一块，另外 3584 B 会变成 0xFF。
 */
#define FATFS_PDRV 0U
#define FATFS_SECTOR 512U
#define FATFS_ERASE 4096U
#define FATFS_ERASE_SECTORS (FATFS_ERASE / FATFS_SECTOR)

/* 目录项的卷标位（0x08）。ff.h 只给了 AM_RDO/HID/SYS/DIR/ARC，没有 AM_VOL ——
 * R0.15 把"卷标"当成格式化时才写的一条特殊项，不对外暴露宏。卷标不是文件，
 * 不跳过它会让"根目录里有几个文件"永远多 1。 */
#define FATFS_ATTR_VOL 0x08U

static FATFS s_fs;
static int s_mounted;
static int s_last_error;

/* diskio 适配：签名必须与 ff.c 的调用严格一致（diskio.h 是唯一来源）。
 * 全部返回值按 FatFs 的口径：STA_NOINIT 表示"没初始化"，RES_ERROR 表示
 * "这一块真的读/写失败了" —— 一律用 RES_ERROR 而不是 RES_NOTRDY 糊过去，
 * 否则挂载会退化成"空卷"，比报错更难查。 */

DSTATUS disk_status(BYTE pdrv)
{
    if ((pdrv != FATFS_PDRV) || (bsl_device_is_ready(BSL_DEV_NOR) == 0)) {
        return STA_NOINIT;
    }
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    /* NOR 的初始化在 bsl_init 里做完了（BOARD 级），这里不重复上电。
     * 只报状态，避免第二次 board_nor_init 去重配 SPI/CS。 */
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    const bsl_nor_api_t *nor;
    uint32_t addr;
    uint32_t len;

    if (disk_status(pdrv) != 0U || buff == NULL || count == 0U) {
        return RES_ERROR;
    }
    nor = bsl_devapi_nor();
    /* 读不需要对齐：NOR 支持任意地址读，512 B 落在 4 KB 块中间的偏移
     * 直接照地址读就行，不必先读出整块再切窗口。 */
    addr = (uint32_t)sector * FATFS_SECTOR;
    len = (uint32_t)count * FATFS_SECTOR;
    if (nor->read(addr, buff, len) != BSL_OK) {
        return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    static uint8_t s_rmw[FATFS_ERASE];
    const bsl_nor_api_t *nor;
    UINT i;

    if (disk_status(pdrv) != 0U || buff == NULL || count == 0U) {
        return RES_ERROR;
    }
    nor = bsl_devapi_nor();

    /* NOR 只能把 1 写成 0，且最小可擦单位是 4 KB —— 而 FatFs 一次只给一块
     * 512 B。所以每个逻辑扇区都要走"读整块 → 替换窗口 → 擦 → 写回"。
     *
     * 不做批量合并（例如连续 8 个扇区刚好铺满一块时省掉读回）：那是优化，
     * 先把语义做对。合并写错的表现是"写进去的文件头对、后面几百字节变 0xFF"，
     * 比慢更难查。
     *
     * 按扇区逐个擦写，不是"全擦完再写"：中途失败时已擦的区域不至于从 1 块
     * 扩大到 count 块。 */
    for (i = 0U; i < count; i++) {
        uint32_t base = (uint32_t)(sector + i) * FATFS_SECTOR;
        uint32_t blk = base & ~(FATFS_ERASE - 1U);
        uint32_t off = base - blk;
        const uint8_t *src = buff + (uint32_t)i * FATFS_SECTOR;

        if (nor->read(blk, s_rmw, FATFS_ERASE) != BSL_OK) {
            return RES_ERROR;
        }
        (void)memcpy(&s_rmw[off], src, FATFS_SECTOR);
        if (nor->erase_sector(blk) != BSL_OK) {
            return RES_ERROR;
        }
        if (nor->write(blk, s_rmw, FATFS_ERASE) != BSL_OK) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    const bsl_nor_api_t *nor;

    if (disk_status(pdrv) != 0U) {
        return RES_ERROR;
    }
    nor = bsl_devapi_nor();
    switch (cmd) {
    case CTRL_SYNC:
        /* 写是同步完成的（每次写完都等 WIP 落），不需要 flush。 */
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (buff == NULL) {
            return RES_PARERR;
        }
        *(LBA_t *)buff = (LBA_t)(nor->size() / FATFS_SECTOR);
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (buff == NULL) {
            return RES_PARERR;
        }
        *(WORD *)buff = (WORD)FATFS_SECTOR;
        return RES_OK;
    case GET_BLOCK_SIZE:
        /* 擦除块 = 8 个逻辑扇区（4 KB）。给 0 会让 f_mkfs 以为"块大小未知"；
         * 给 1 会让它以为"每 512 B 就能单独擦"，格式化时按错误粒度排布。 */
        if (buff == NULL) {
            return RES_PARERR;
        }
        *(DWORD *)buff = (DWORD)FATFS_ERASE_SECTORS;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

bsl_err_t fml_fatfs_mount(void)
{
    FRESULT fr;

    if (bsl_device_is_ready(BSL_DEV_NOR) == 0) {
        return BSL_ERR_INVAL;
    }
    /* opt = 1：立即挂载。opt = 0 会把 I/O 推迟到首次访问，
     * 那样 POST 打出来的"挂载成功"其实什么都没验。 */
    fr = f_mount(&s_fs, FML_FATFS_VOL, 1);
    s_last_error = (int)fr;
    if (fr != FR_OK) {
        s_mounted = 0;
        return BSL_ERR_IO;
    }
    s_mounted = 1;
    return BSL_OK;
}

int fml_fatfs_mounted(void)
{
    return s_mounted;
}

uint32_t fml_fatfs_total_kb(void)
{
    uint64_t bytes;

    if ((s_mounted == 0) || (s_fs.n_fatent <= 2U)) {
        return 0U;
    }
    /* 有效簇数 = n_fatent - 2（FAT 表头两个保留项）；csize 的单位是"扇区"，
     * 而本卷的扇区是 512 B。FATFS 结构里没有 ssize 字段可读（FF_MAX_SS ==
     * FF_MIN_SS 时它被裁掉），所以只能用编译期常量 FATFS_SECTOR。 */
    bytes = ((uint64_t)s_fs.n_fatent - 2U) * s_fs.csize * FATFS_SECTOR;
    return (uint32_t)(bytes >> 10);
}

bsl_err_t fml_fatfs_list_dir(const char *path, fml_fatfs_dirent_t *buf, unsigned max,
                             unsigned *count_out)
{
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    unsigned n = 0U;
    unsigned i;

    if ((s_mounted == 0) || (path == NULL) || (count_out == NULL) ||
        ((buf == NULL) && (max > 0U))) {
        return BSL_ERR_INVAL;
    }
    *count_out = 0U;

    fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        s_last_error = (int)fr;
        return BSL_ERR_IO;
    }
    for (;;) {
        fr = f_readdir(&dir, &fno);
        if ((fr != FR_OK) || (fno.fname[0] == '\0')) {
            break;
        }
        /* 卷标项不是文件，不计入也不外传 —— 否则"根目录里有几个文件"会被
         * 卷标一直撑大 1，而没人知道为什么。 */
        if ((fno.fattrib & FATFS_ATTR_VOL) != 0U) {
            continue;
        }
        if (n < max) {
            for (i = 0U; (i + 1U < FML_FATFS_LNAME_LEN) && (fno.fname[i] != '\0'); i++) {
                buf[n].name[i] = fno.fname[i];
            }
            buf[n].name[i] = '\0';
            /* altname 只在 LFN 启用时被填；为空就退回 fname，让 sname 永远
             * 可读（日志要能看，且 sname 是 ASCII）。 */
            for (i = 0U; (i + 1U < FML_FATFS_SNAME_LEN) && (fno.altname[i] != '\0'); i++) {
                buf[n].sname[i] = fno.altname[i];
            }
            buf[n].sname[i] = '\0';
            if (buf[n].sname[0] == '\0') {
                for (i = 0U; (i + 1U < FML_FATFS_SNAME_LEN) && (buf[n].name[i] != '\0'); i++) {
                    buf[n].sname[i] = buf[n].name[i];
                }
                buf[n].sname[i] = '\0';
            }
            buf[n].size = (uint32_t)fno.fsize;
            buf[n].is_dir = ((fno.fattrib & AM_DIR) != 0U) ? 1U : 0U;
        }
        n++;
    }
    (void)f_closedir(&dir);
    if (fr != FR_OK) {
        s_last_error = (int)fr;
        return BSL_ERR_IO;
    }
    *count_out = n;
    return BSL_OK;
}

int fml_fatfs_last_error(void)
{
    return s_last_error;
}
