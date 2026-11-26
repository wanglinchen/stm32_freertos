#include "board_nand.h"

#include "board_rt.h"
#include "bsl_gpio.h"
#include "bsl_log.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>

/* NAND（P9a）只读通道。事实来源：参考工程 Drivers/BSP/NAND/nand.c。
 *
 * 总线：FMC NAND，8 位，CLE=A16、ALE=A17（地址线偏移固定由 FMC 定义，
 * 不是板级可选项，所以放这里而不是 DTB 属性）；命令/地址/数据三者在同一
 * 基址上按位选通。
 * 引脚（AF12，与 8080 屏共用同一组 FMC 信号，重复配置不改变任何一位）：
 *   PD0/PD1 = D2/D3，PD4 = NRE，PD5 = NWE，PD11 = CLE，PD12 = ALE，
 *   PD14/PD15 = D0/D1，PE7..PE10 = D4..D7，PG9 = NCE，PD6 = R/B（输入上拉）。
 * PG10 是本板的 NRF_CS，不能拿来做 NCE；参考工程同样用 PG9。
 *
 * 时序寄存器照参考工程：PCR = PTYP(1<<3) | ECCPS(1<<17) | TCLR(1<<9) | TAR(1<<13)，
 * ECC 关闭（本批不做 ECC 校验，读出来的是原始数据）；PMEM = MEMSET 2 /
 * MEMWAIT 3 / MEMHOLD 2 / MEMHIZ 2。
 *
 * 为什么两个 bank 都配：参考工程用 0x80000000（Bank3/PCR3），但片选落在 PG9，
 * 而 F429 的 PG9 在多数封装上是 NCE2、PG10 才是 NCE3。两个 bank 用同一组时序
 * 一起配好、逐个探 ID，谁能读出合法 ID 就用谁 —— 不把"到底哪个 bank"写死成
 * 一个猜出来的常量。 */
#define NAND_BANK3_BASE 0x80000000UL
#define NAND_BANK2_BASE 0x70000000UL

#define NAND_CMD_OFF 0x00010000UL /* A16 = CLE */
#define NAND_ADDR_OFF 0x00020000UL /* A17 = ALE */

#define NAND_CMD_RESET 0xFFU
#define NAND_CMD_READID 0x90U
#define NAND_CMD_STATUS 0x70U
#define NAND_CMD_READ 0x00U
#define NAND_CMD_READ_TRUE 0x30U
#define NAND_STATUS_READY 0x40U

/* 参考工程的三个等待量（循环次数，不是纳秒）：tADL 30 / tWHR 25 / tRHW 35。
 * 168 MHz 下一次循环 ≈ 4 个周期，30 次 ≈ 0.7 µs，远大于数据手册的 70 ns。 */
#define NAND_TADL 30U
#define NAND_TWHR 25U
#define NAND_TRHW 35U

/* 等待上限：一条读页命令典型 25 µs、最坏 60 µs（[数据手册 tR]）。按 168 MHz
 * 下空循环取 200000 次（≈ 数毫秒）已经留了两个数量级余量；超时就返回 TIMEOUT，
 * 不无限等 —— 板上没插片时 POST 得能结束。 */
#define NAND_POLL_LIMIT 200000UL

static uint32_t s_base;
static unsigned s_bank;
static int s_ready;
static uint8_t s_id[5];
static uint8_t s_bad3[5];
static uint8_t s_bad2[5];
static board_rt_nand_t s_cfg;

static void nand_delay(volatile uint32_t n)
{
    while (n > 0U) {
        n--;
    }
}

static uint8_t rd8(uint32_t addr)
{
    return *(volatile uint8_t *)addr;
}

static void wr8(uint32_t addr, uint8_t v)
{
    *(volatile uint8_t *)addr = v;
}

static void nand_cmd(uint8_t cmd)
{
    wr8(s_base | NAND_CMD_OFF, cmd);
    nand_delay(NAND_TADL);
}

static void nand_addr(uint8_t a)
{
    wr8(s_base | NAND_ADDR_OFF, a);
    nand_delay(NAND_TADL);
}

static uint8_t nand_status(void)
{
    nand_cmd(NAND_CMD_STATUS);
    nand_delay(NAND_TWHR);
    /* tWHR：读状态前必须等 ≥60 ns。168 MHz 下一次函数返回已经够，这里再
     * 空转 16 次把余量做实（[数据手册 tWHR]）。 */
    for (volatile int i = 0; i < 16; i++) {
    }
    return rd8(s_base);
}

static int nand_wait_ready(void)
{
    for (uint32_t n = 0U; n < NAND_POLL_LIMIT; n++) {
        if ((nand_status() & NAND_STATUS_READY) != 0U) {
            return 1;
        }
    }
    return 0;
}

/* 等 R/B 回到高电平（就绪）。用 DTB 给的 R/B 脚（本板 GPIOD.6），**不碰状态寄存器**。
 *
 * 这一条是 P9a-2 挖出来的现场：0x30（真读）之后如果再用 0x70 查状态，
 * 器件输出就被切成"状态字节"，之后从数据口读到的是 2048 个 0xE0 ——
 * 也就是"就绪态的状态字节"，不是页数据。P9a 记的 sum=00070000/ff=0 正是
 * 2048 × 0xE0，说明那一批的"读页"读的是状态而不是页；判据没过是因为
 * 判据本身就没在验内容。所以数据路径一律改走 R/B 脚，状态寄存器只在
 * 擦/写这类"一次性查 I/O0"的场合用。 */
static int nand_wait_rb(void)
{
    if ((s_cfg.rb_port == NULL) || (s_cfg.rb_mask == 0U)) {
        return 0;
    }
    for (uint32_t n = 0U; n < NAND_POLL_LIMIT; n++) {
        if (bsl_gpio_read(s_cfg.rb_port, s_cfg.rb_mask) == GPIO_PIN_SET) {
            return 1;
        }
    }
    return 0;
}

static int nand_read_id(uint8_t id[5])
{
    nand_cmd(NAND_CMD_READID);
    nand_addr(0x00U);
    nand_delay(NAND_TWHR);
    for (unsigned i = 0U; i < 5U; i++) {
        id[i] = rd8(s_base);
    }
    /* 合法 ID 的判据是「厂商无关」的：首字节 -- 非 0x00/0xFF。
 * 本板实测 5 字节 = AD DC 90 95 56，首字节 0xAD 是 SK Hynix（Micron 才是 0x2C）。
 * 原厂自检只报 "NAND 512MB OK"、不含型号；把 0x2C 写死会把一片好片子判失败。
 * 容量由 DTB 与 ID 分别进证据，型号不猜。 */
    if (id[0] == 0x00U || id[0] == 0xFFU) {
        return 0;
    }
    for (unsigned i = 1U; i < 5U; i++) {
        if (id[i] != 0x00U && id[i] != 0xFFU) {
            return 1;
        }
    }
    return 0;
}

/* 读一页的一段：page = 行地址，col = 页内列地址（0 起，含 spare 区）。
 * 全程用状态寄存器判忙，不依赖 R/B 脚 —— 参考工程用 R/B 先等低再等高，
 * 而 tR 只有 25 µs，快速轮询可能错过"低"这一瞬（参考工程注释里自己写过这个坑）。 */
static bsl_err_t nand_read_page(uint32_t page, uint16_t col, uint8_t *buf,
                               uint16_t len)
{
    if (s_ready == 0 || buf == NULL || len == 0U) {
        return BSL_ERR_NOINIT;
    }
    if ((uint32_t)len + col > s_cfg.page_main + s_cfg.page_spare) {
        return BSL_ERR_INVAL;
    }
    nand_cmd(NAND_CMD_READ);
    nand_addr((uint8_t)col);
    nand_addr((uint8_t)(col >> 8));
    nand_addr((uint8_t)page);
    nand_addr((uint8_t)(page >> 8));
    nand_addr((uint8_t)(page >> 16));
    nand_cmd(NAND_CMD_READ_TRUE);
    nand_delay(NAND_TRHW);

    if ((s_cfg.rb_port != NULL) && (s_cfg.rb_mask != 0U)) {
        if (nand_wait_rb() == 0) {
            return BSL_ERR_TIMEOUT;
        }
    } else {
        /* 没有 R/B 脚时只能查状态 —— 但查完状态器件输出的是状态字节，
         * 必须把 0x00/地址/0x30 再走一遍才能读到页数据（tR 已经过去，不再等）。 */
        if (nand_wait_ready() == 0) {
            return BSL_ERR_TIMEOUT;
        }
        nand_cmd(NAND_CMD_READ);
        nand_addr((uint8_t)col);
        nand_addr((uint8_t)(col >> 8));
        nand_addr((uint8_t)page);
        nand_addr((uint8_t)(page >> 8));
        nand_addr((uint8_t)(page >> 16));
        nand_cmd(NAND_CMD_READ_TRUE);
        nand_delay(NAND_TRHW);
    }
    for (uint16_t i = 0U; i < len; i++) {
        buf[i] = rd8(s_base);
    }
    return BSL_OK;
}

/* ======================================================================
 * 写通道（P9a-2）：擦一块 / 写一页 / 判坏块
 *
 * 三条命令序列照 MT29F4G08 的 8 位器件口径（[数据手册]）：
 *   写：0x80 → 列地址 2 拍 → 行地址 3 拍 → 数据 → 0x10 → 等 R/B → 读状态
 *   擦：0x60 → 行地址 3 拍 → 0xD0 → 等 R/B → 读状态
 *   状态：0x70，I/O0 = 1 表示这一步失败，I/O6 = 1 表示就绪
 * 行地址就是页号本身（A12.. 起），与 read_page 里那三拍完全一致 —— 不另算一套。
 *
 * 刻意**不做**的事：不写 spare（擦除会把它自然回 0xFF）、不开硬件 ECC
 * （FMC PCR 的 ECCEN=0，写进去的就是写进去的）、不建坏块 LUT、不做 FTL。
 * 写通道在这批里只是"能写、能判"的原料，FTL 是另一批的事。
 * ==================================================================== */

#define NAND_CMD_WRITE 0x80U
#define NAND_CMD_WRITE_TRUE 0x10U
#define NAND_CMD_ERASE1 0x60U
#define NAND_CMD_ERASE2 0xD0U
#define NAND_STATUS_FAIL 0x01U
#define NAND_SPARE_BAD_MARK 0xFFU

/* 等一次"忙 → 就绪"：先等 R/B 真的落下（擦/编程的忙期是 ms 级，窗口够宽），
 * 再等它回到高。**不能只查状态寄存器**——0x70 在器件还没开始忙之前读到的是
 * "就绪"，会提前返回，于是"写成功但读回全不一样"。没有 R/B 脚时退回状态轮询。 */
static int nand_wait_rb_busy(void)
{
    if ((s_cfg.rb_port == NULL) || (s_cfg.rb_mask == 0U)) {
        return nand_wait_ready();
    }
    for (uint32_t n = 0U; n < 20000U; n++) {
        if (bsl_gpio_read(s_cfg.rb_port, s_cfg.rb_mask) == GPIO_PIN_RESET) {
            break;
        }
    }
    return nand_wait_rb();
}

/* 擦一个块：block 是块号。忙等先等 R/B 落下再等它回高（理由见 nand_wait_rb_busy：
 * 只查状态寄存器会在器件还没开始忙时读到"就绪"，那是"写成功但读回全不一样"的真因）。 */
static bsl_err_t nand_erase_block(uint32_t block)
{
    uint32_t page;

    if (s_ready == 0) {
        return BSL_ERR_NOINIT;
    }
    if (block >= s_cfg.blocks) {
        return BSL_ERR_INVAL;
    }
    if (nand_wait_ready() == 0) {
        return BSL_ERR_TIMEOUT;
    }
    page = block * s_cfg.block_pages;
    nand_cmd(NAND_CMD_ERASE1);
    /* 行地址 3 拍，低位在前 —— 与 read_page 里那三拍同序（首版写成高位在前，
     * 上板 wr_er=0 就是这个：擦命令落到别的块地址上，状态寄存器自然报失败）。 */
    nand_addr((uint8_t)page);
    nand_addr((uint8_t)(page >> 8));
    nand_addr((uint8_t)(page >> 16));
    nand_cmd(NAND_CMD_ERASE2);
    nand_delay(NAND_TRHW);
    if (nand_wait_rb_busy() == 0) {
        return BSL_ERR_TIMEOUT;
    }
    if ((nand_status() & NAND_STATUS_FAIL) != 0U) {
        return BSL_ERR_IO;
    }
    return BSL_OK;
}

/* 写一页的一段：只写主区/或含 spare，按 col+len 给，地址拍与读一致。 */
static bsl_err_t nand_write_page(uint32_t page, uint16_t col,
                                 const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (s_ready == 0) {
        return BSL_ERR_NOINIT;
    }
    if ((buf == NULL) || (len == 0U)) {
        return BSL_ERR_INVAL;
    }
    if ((uint32_t)len + col > s_cfg.page_main + s_cfg.page_spare) {
        return BSL_ERR_INVAL;
    }
    if (page >= (s_cfg.blocks * s_cfg.block_pages)) {
        return BSL_ERR_INVAL;
    }
    if (nand_wait_ready() == 0) {
        return BSL_ERR_TIMEOUT;
    }
    nand_cmd(NAND_CMD_WRITE);
    nand_addr((uint8_t)col);
    nand_addr((uint8_t)(col >> 8));
    nand_addr((uint8_t)page);
    nand_addr((uint8_t)(page >> 8));
    nand_addr((uint8_t)(page >> 16));
    for (i = 0U; i < len; i++) {
        wr8(s_base, buf[i]);
    }
    nand_cmd(NAND_CMD_WRITE_TRUE);
    nand_delay(NAND_TRHW);
    if (nand_wait_rb_busy() == 0) {
        return BSL_ERR_TIMEOUT;
    }
    if ((nand_status() & NAND_STATUS_FAIL) != 0U) {
        return BSL_ERR_IO;
    }
    return BSL_OK;
}

/* 坏块判定：读该块第 0 页 spare 的首字节，非 0xFF 即为出厂坏块标记。
 * 读不出来也当坏块 —— 判不准的时候不许往里写，这是唯一安全的方向。 */
static int nand_is_bad_block(uint32_t block)
{
    uint8_t mark = 0x00U;

    if (s_ready == 0 || block >= s_cfg.blocks) {
        return 1;
    }
    if (nand_read_page(block * s_cfg.block_pages, (uint16_t)s_cfg.page_main,
                       &mark, 1U) != BSL_OK) {
        return 1;
    }
    return (mark != NAND_SPARE_BAD_MARK) ? 1 : 0;
}

static void nand_bank_config(void)
{
    /* PCR：PTYP=1(NAND) | PWID=0(8 位) | ECCEN=0 | ECCPS=1(512 B 页) |
     * TCLR=1 | TAR=1（[参考工程 nand_init]）。 */
    uint32_t pcr = (1UL << 3) | (1UL << 17) | (1UL << 9) | (1UL << 13);
    /* PMEM：MEMSET=2 | MEMWAIT=3 | MEMHOLD=2 | MEMHIZ=2（同上）。 */
    uint32_t pmem = 2UL | (3UL << 8) | (2UL << 16) | (2UL << 24);

    FMC_Bank2_3->PCR2 = pcr;
    FMC_Bank2_3->PMEM2 = pmem;
    FMC_Bank2_3->PCR3 = pcr;
    FMC_Bank2_3->PMEM3 = pmem;
    FMC_Bank2_3->PCR2 |= 1UL << 2; /* PBKEN */
    FMC_Bank2_3->PCR3 |= 1UL << 2;
}

static void nand_pins_init(void)
{
    (void)bsl_gpio_init_af(GPIOD, 0xdc33U, 12U, GPIO_PULLUP,
                           GPIO_SPEED_FREQ_VERY_HIGH);
    (void)bsl_gpio_init_af(GPIOE, 0x0780U, 12U, GPIO_PULLUP,
                           GPIO_SPEED_FREQ_VERY_HIGH);
    (void)bsl_gpio_init_af(GPIOG, 0x0200U, 12U, GPIO_PULLUP,
                           GPIO_SPEED_FREQ_VERY_HIGH);

    /* R/B（PD6）是普通输入，上拉：读 ID / 读状态都不用它，但留着这条
     * 是为了和参考工程同一份引脚事实 —— 哪天要用 R/B 计时，脚已经是对的。 */
    if (s_cfg.rb_mask != 0U) {
        (void)bsl_gpio_init_input(s_cfg.rb_port, s_cfg.rb_mask, GPIO_PULLUP,
                                 GPIO_SPEED_FREQ_LOW);
    }
}

bsl_err_t board_nand_init(void)
{
    if (board_rt_nand(&s_cfg) == 0) {
        return BSL_ERR_INVAL;
    }
    if (s_cfg.page_main == 0U || s_cfg.page_spare == 0U
        || s_cfg.block_pages == 0U || s_cfg.blocks == 0U) {
        return BSL_ERR_INVAL;
    }

    nand_pins_init();
    nand_bank_config();

    s_base = NAND_BANK3_BASE;
    s_bank = 3U;
    nand_cmd(NAND_CMD_RESET);
    (void)nand_wait_ready();
    if (nand_read_id(s_id) == 0) {
        s_bad3[0]=s_id[0];
        s_bad3[1]=s_id[1];
        s_bad3[2]=s_id[2];
        s_bad3[3]=s_id[3];
        s_bad3[4]=s_id[4];
        s_base = NAND_BANK2_BASE;
        s_bank = 2U;
        nand_cmd(NAND_CMD_RESET);
        (void)nand_wait_ready();
        if (nand_read_id(s_id) == 0) {
            s_bad2[0]=s_id[0];
            s_bad2[1]=s_id[1];
            s_bad2[2]=s_id[2];
            s_bad2[3]=s_id[3];
            s_bad2[4]=s_id[4];
            s_ready = 0;
            bsl_log_puts("[bsl] nand raw ");
            for (unsigned i = 0U; i < 5U; i++) {
                bsl_log_put_hex(s_bad3[i], 2U);
                bsl_log_puts(" ");
            }
            bsl_log_puts("| ");
            for (unsigned i = 0U; i < 5U; i++) {
                bsl_log_put_hex(s_bad2[i], 2U);
                bsl_log_puts(" ");
            }
            bsl_log_puts("\r\n");
            return BSL_ERR_IO;
        }
    }
    s_ready = 1;
    return BSL_OK;
}

static int nand_api_ready(void)
{
    return s_ready;
}

static void nand_api_info(uint8_t id[5], uint16_t *page_main,
                          uint16_t *page_spare, uint32_t *block_pages,
                          uint32_t *blocks, unsigned *bank)
{
    if (id != NULL) {
        for (unsigned i = 0U; i < 5U; i++) {
            id[i] = s_id[i];
        }
    }
    if (page_main != NULL) {
        *page_main = s_cfg.page_main;
    }
    if (page_spare != NULL) {
        *page_spare = s_cfg.page_spare;
    }
    if (block_pages != NULL) {
        *block_pages = s_cfg.block_pages;
    }
    if (blocks != NULL) {
        *blocks = s_cfg.blocks;
    }
    if (bank != NULL) {
        *bank = s_bank;
    }
}

const bsl_nand_api_t bsl_api_brd_nand = {
    .ready = nand_api_ready,
    .info = nand_api_info,
    .read_page = nand_read_page,
    .erase_block = nand_erase_block,
    .write_page = nand_write_page,
    .is_bad_block = nand_is_bad_block,
};

static bsl_err_t nand_driver_init(void)
{
    return board_nand_init();
}

const bsl_device_driver_t bsl_driver_brd_nand = {
    .init = nand_driver_init,
};
