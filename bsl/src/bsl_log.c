#include "bsl_log.h"

#include "stm32f4xx_hal.h"   /* __get_PRIMASK / __disable_irq：不引 FreeRTOS，bsl 的允许依赖只有 hal */

#include "board_rt.h"
#include "bsl_device.h"
#include "board_debug_uart.h"

static int s_banner_done;

/*
 * 环形日志：给"故障前那几行"留一个不被 UART 时序吞掉的副本。
 *
 * 早前日志只有一个去向 —— 当场写给 UART，写完即消失。于是 panic 时串口若没接、
 * 或故障发生在 UART 就绪之前，导致故障的那几行一起蒸发，现场只剩"板子停住了"。
 * 这里每条以 '\n' 结束的行都另存一份到环形槽，panic 前由 fml 调
 * bsl_log_dump_ring() 倒出来。
 *
 * 不落 .noinit：跨复位保留需要另加 magic + 索引校验，而 .noinit 已在承载
 * panic 记录（同段落两次写入会让地址表与地址判据一起漂移）。当前形态覆盖
 * "运行期 panic"这一主要场景；"UART 在 panic 之前就坏了"记为敞口。
 *
 * 记录与 UART 就绪解耦：先入 ring，再决定要不要写 UART —— 反过来会让"串口
 * 坏掉时发生的故障"连缓冲区都留不下。
 */
#define BSL_LOG_RING_LINES 8U
#define BSL_LOG_RING_LINE_CHARS 40U

static char s_ring[BSL_LOG_RING_LINES][BSL_LOG_RING_LINE_CHARS];
static char s_line[BSL_LOG_RING_LINE_CHARS];
static unsigned s_head;
static unsigned s_line_len;

/* UART 输出的占用标志：HAL_UART_Transmit 非重入，两处同时写会把两条字符串
 * 的字节交错。占不住就**只入 ring、不写串口** —— ring 里仍有完整副本，
 * 这比把两条日志搅成一条更难读要好。
 *
 * 占住以后必须能还回去。若 board_debug_uart_puts() 在 HAL 里卡住（等 TC 标志的
 * 循环碰上时钟/引脚问题，或返回路径没跑到清标志），标志就永远为 1，此后所有
 * 日志都不再上屏 —— 而且**没有任何提示**：现场看到的是"程序在跑但串口没动静"，
 * 与"日志本来就没打"无法区分。所以加两道：
 *   1. 超时自愈：占用超过 BSL_LOG_UART_BUSY_TIMEOUT_MS 就判定上一次卡住了，
 *      强制收回。115200 下 60 B 一行约 5.2 ms，50 ms 是十倍余量。
 *   2. 丢行计数：因占用未清而没有上屏的行数，在 banner 与 panic dump 里打出来。
 */
#define BSL_LOG_UART_BUSY_TIMEOUT_MS 50U

static int s_uart_busy;
static uint32_t s_uart_busy_tick;   /* 抢到占用的时刻（HAL_GetTick） */
static unsigned s_uart_drops;       /* 因占用未清而没有上屏的行数 */

/* 环形保护的临界区：保存并恢复 PRIMASK，因此可嵌套（panic 路径已经在关中断
 * 状态下调用时不会被这里的退出提前开中断）。
 *
 * 为什么不用 FreeRTOS 的 taskENTER_CRITICAL：`LAYER_DEPENDENCIES["bsl"]` 只允许
 * `hal`，bsl 一旦 include 内核头就越级。PRIMASK 关中断同样能挡住任务切换
 * （PendSV 也被屏蔽），对"几十字节的拷贝"够用且是微秒级。 */
static uint32_t log_irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void log_irq_restore(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/* 读丢行计数也要进临界区：写入点在 bsl_log_puts 的临界区内，32 位读虽然单次
 * 总线不可分，但读到的值要与 banner 那句判断一致。 */
static unsigned uart_drop_count(void)
{
    uint32_t primask = log_irq_save();
    unsigned n = s_uart_drops;

    log_irq_restore(primask);
    return n;
}

/* dump 路径不能用 bsl_log_put_dec：它会经 bsl_log_puts 写回 ring，把要倒出来的
 * 历史覆盖掉。这里只往串口吐字符。 */
static void dump_uart_dec(unsigned v)
{
    char buf[11];
    unsigned i = 0U;

    if (v == 0U) {
        board_debug_uart_puts("0");
        return;
    }
    while (v > 0U && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (i > 0U) {
        char ch[2];

        ch[0] = buf[--i];
        ch[1] = '\0';
        board_debug_uart_puts(ch);
    }
}

static void ring_commit_line(void)
{
    unsigned i;

    for (i = 0U; i < BSL_LOG_RING_LINE_CHARS; i++) {
        s_ring[s_head][i] = s_line[i];
    }
    s_head = (s_head + 1U) % BSL_LOG_RING_LINES;
    s_line_len = 0U;
}

static void ring_absorb(const char *s)
{
    char c;

    while (*s != '\0') {
        c = *s++;
        if (s_line_len < (BSL_LOG_RING_LINE_CHARS - 1U)) {
            s_line[s_line_len++] = c;
        }
        if (c == '\n') {
            ring_commit_line();
        }
    }
}

/*
 * 日志的唯一出口，也是"UART 没起来时能不能写"的唯一判断点。
 *
 * board_uart_write() 直接把 huart 交给 HAL_UART_Transmit，而 debug_uart 初始化
 * 失败时 huart.Instance 还是 NULL —— 一写就是解引用空指针的 HardFault。
 *
 * 这条路以前不可达：`bsl_board_periph_init()` 在 POST_CLOCK 失败时直接 return，
 * BOARD 级整组不跑，fml 任务随即拿不到 api 而 panic(2)，整机先复位了。两级都跑
 * 之后，"UART 失败、按键/LED 成功"是可实现的组合 —— 按键一按下就走
 * sample_log_keys() 打日志，HardFault 比原来的复位循环更难查。
 *
 * 所以判断收在这一处：所有 BSL_LOG_* 都经 bsl_log_puts()，未就绪即空操作。
 * 消费方不必各自判（bsl_init.c 那处仍保留，它要决定的是"要不要构造整条日志"）。
 */
void bsl_log_puts(const char *s)
{
    uint32_t primask;
    int uart_ok;

    if (s == NULL) {
        return;
    }
    /* 顺序是"先入 ring、再写 UART"：UART 未就绪时 ring 仍要留下这一行，
     * 否则"串口坏掉之后发生的故障"连故障前的日志也读不到。
     *
     * 入 ring 必须在临界区内：`s_ring` / `s_line` / `s_head` / `s_line_len` 是
     * 全层共享的 static，样例应用的两个任务（LED、按键）都会写。不保护时两条
     * 日志会互相撕裂——`s_line` 被第二方从中间追加、`s_head` 被两边各加一次
     * 于是一槽内容直接消失。故障前的那几行正是排查起点，坏了比没有更误导。 */
    primask = log_irq_save();
    ring_absorb(s);
    uart_ok = 0;
    if (bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
        if ((s_uart_busy != 0)
            && ((uint32_t)(HAL_GetTick() - s_uart_busy_tick) >= BSL_LOG_UART_BUSY_TIMEOUT_MS)) {
            /* 上一次调用卡住了：收回占用，否则日志从此永久不上屏。 */
            s_uart_busy = 0;
        }
        if (s_uart_busy == 0) {
            s_uart_busy = 1;
            s_uart_busy_tick = HAL_GetTick();
            uart_ok = 1;
        } else {
            s_uart_drops++;
        }
    }
    log_irq_restore(primask);

    if (!uart_ok) {
        return;
    }
    board_debug_uart_puts(s);
    primask = log_irq_save();
    s_uart_busy = 0;
    log_irq_restore(primask);
}

/*
 * 启动 banner：实例名与波特率在运行期从 DTB 的 debug_serial 节点取，
 * 不再来自构建期生成的 BOARD_DEBUG_UART_BANNER。
 *
 * 早前这句写在 fml_led.c 里是硬编码 "USART1 115200"：换 USART2 / 改波特率后仍照
 * 原样打，现场会按错的实例与速率排障（链路其实是好的，所以没有任何别的信号能
 * 纠正这个判断）。fml 拿不到实例名 —— C 没有把 `USART1` 宏字符串化的通道 ——
 * 所以文案留在这里，fml 只调一次。
 *
 * 未就绪时不消耗这次机会：UART 没起来就什么都不打、也不置位。
 */
static const char *debug_uart_instance_name(uintptr_t reg)
{
    switch (reg) {
    case 0x40011000U: return "USART1";
    case 0x40004400U: return "USART2";
    case 0x40004800U: return "USART3";
    case 0x40004C00U: return "UART4";
    case 0x40005000U: return "UART5";
    case 0x40011400U: return "USART6";
    default: return "UART?";
    }
}

void bsl_log_startup_banner(void)
{
    uintptr_t reg = 0U;
    uint32_t baud = 0U;
    char tx_port = '\0';
    char rx_port = '\0';
    unsigned tx_pin = 0U;
    unsigned rx_pin = 0U;

    if (s_banner_done != 0) {
        return;
    }
    if (!bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
        return;
    }
    if (!board_rt_uart(&reg, &baud, &tx_port, &tx_pin, &rx_port, &rx_pin)) {
        return;
    }
    s_banner_done = 1;
    bsl_log_puts("\r\nstm32-freertos: ");
    bsl_log_puts(debug_uart_instance_name(reg));
    bsl_log_puts(" ");
    bsl_log_put_dec((int)baud);
    bsl_log_puts(" debug ready");
    if (uart_drop_count() != 0U) {
        /* 有行在启动阶段就被 UART 占用挡掉了：读者看到的第一屏日志不完整，
         * 这里直接告诉他少了多少行，免得把"少"当成"没有发生"。 */
        bsl_log_puts(" (uart busy: ");
        bsl_log_put_dec((int)uart_drop_count());
        bsl_log_puts(" line(s) not shown)");
    }
    bsl_log_puts("\r\n");
}

void bsl_log_mod_puts(const char *mod, const char *s)
{
    if (mod == NULL || mod[0] == '\0') {
        bsl_log_puts(s);
        return;
    }
    bsl_log_puts("[");
    bsl_log_puts(mod);
    bsl_log_puts("] ");
    bsl_log_puts(s);
}

void bsl_log_put_dec(int value)
{
    char buf[12];
    unsigned u;
    unsigned i = 0U;
    unsigned start;

    if (value < 0) {
        bsl_log_puts("-");
        u = (unsigned)(-(value + 1)) + 1U;
    } else {
        u = (unsigned)value;
    }
    if (u == 0U) {
        bsl_log_puts("0");
        return;
    }
    while (u > 0U && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (u % 10U));
        u /= 10U;
    }
    for (start = 0U; start < i; start++) {
        char ch[2] = {buf[i - 1U - start], '\0'};
        bsl_log_puts(ch);
    }
}

/*
 * 定宽十六进制（digits 位，1..8）。给"必须能和手册逐字节对"的值用：
 * JEDEC ID、寄存器读回、地址。十进制在这类值上没有可比性 —— 0xEF4019 打成
 * 15679513 之后没人能一眼看出它是 Winbond。
 * digits 越界按 1..8 夹住，不返回错误：日志函数不该在错误路径上再制造分支。
 */
void bsl_log_put_hex(uint32_t value, unsigned digits)
{
    static const char hex[] = "0123456789abcdef";
    char ch[2] = {'\0', '\0'};
    unsigned shift;

    if (digits == 0U) {
        digits = 1U;
    }
    if (digits > 8U) {
        digits = 8U;
    }
    for (shift = digits; shift > 0U; shift--) {
        ch[0] = hex[(value >> ((shift - 1U) * 4U)) & 0xFU];
        bsl_log_puts(ch);
    }
}

/*
 * 把环形里的行按"最老 → 最新"倒出来。只给 panic 路径用。
 *
 * 直接调 board_debug_uart_puts 而不经 bsl_log_puts：后者会把这些行再写进 ring，
 * 倒一次就把缓冲区覆盖成"dump 自身的日志"，下一层排查反而看不到故障前的内容。
 * 未就绪时不打 —— 这条判断与 bsl_log_puts 同源（UART 未初始化就写 = HardFault）。
 */
void bsl_log_dump_ring(void)
{
    unsigned i;
    const char *line;

    if (!bsl_device_is_ready(BSL_DEV_DEBUG_UART)) {
        return;
    }
    /* 这里**关中断且不再恢复**：本函数只从 panic 路径调用，之后紧接着就是
     * osl_panic_report() 的关中断自旋，所以不需要还回去；同时这也把整个 dump
     * 期间对 ring 的读锁住了 —— 否则别的任务正好在写，倒出来的行会是半新半旧。
     * 代价是 dump 期间（几十毫秒）中断关闭，但此刻本来就要停机。 */
    __disable_irq();
    /* 未提交的半行直接丢弃：它与已提交的最后一行有重叠，打出来会重影。 */
    s_line_len = 0U;
    board_debug_uart_puts("\r\n--- last log lines ---\r\n");
    for (i = 0U; i < BSL_LOG_RING_LINES; i++) {
        line = s_ring[(s_head + i) % BSL_LOG_RING_LINES];
        if (line[0] == '\0') {
            continue;
        }
        board_debug_uart_puts(line);
    }
    board_debug_uart_puts("--- end ---\r\n");
    if (s_uart_drops != 0U) {
        /* 环形是完整的，但其中有若干行当时没上串口（UART 被占用）。
         * 把数量说出来：否则读者会以为环形里的行"本应都在串口上出现过"。 */
        board_debug_uart_puts("--- uart busy: ");
        dump_uart_dec(s_uart_drops);
        board_debug_uart_puts(" line(s) never reached serial ---\r\n");
    }
}
