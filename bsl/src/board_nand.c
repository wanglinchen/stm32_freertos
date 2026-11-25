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

    if (nand_wait_ready() == 0) {
        return BSL_ERR_TIMEOUT;
    }
    for (uint16_t i = 0U; i < len; i++) {
        buf[i] = rd8(s_base);
    }
    return BSL_OK;
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
};

static bsl_err_t nand_driver_init(void)
{
    return board_nand_init();
}

const bsl_device_driver_t bsl_driver_brd_nand = {
    .init = nand_driver_init,
};
