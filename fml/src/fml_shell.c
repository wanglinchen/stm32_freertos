#include "fml_shell.h"

#include <string.h>

#include "board_debug_uart.h"
#include "board_device_registry.h"
#include "bsl_adc.h"
#include "bsl_log.h"
#include "bsl_rtc.h"
#include "osl_thread.h"

/* ---- 输出拼接 ------------------------------------------------------------
 *
 * 一条回复先在内存里拼好，再交给 bsl_log_puts 一次性写 —— 理由见头文件。 */
typedef struct {
    char *p;
    unsigned cap;
    unsigned len;
} sh_out_t;

#define SH_OUT_INIT(buf_, cap_) {(buf_), (cap_), 0U}

static void sh_puts(sh_out_t *o, const char *s)
{
    while ((*s != '\0') && ((o->len + 1U) < o->cap)) {
        o->p[o->len++] = *s++;
    }
    o->p[o->len] = '\0';
}

static void sh_dec(sh_out_t *o, int v)
{
    char tmp[12];
    unsigned n = 0U;
    unsigned u = (unsigned)((v < 0) ? -v : v);

    do {
        tmp[n++] = (char)('0' + (int)(u % 10U));
        u /= 10U;
    } while ((u > 0U) && (n < (unsigned)sizeof(tmp)));
    if ((v < 0) && ((o->len + 1U) < o->cap)) {
        o->p[o->len++] = '-';
    }
    while ((n > 0U) && ((o->len + 1U) < o->cap)) {
        o->p[o->len++] = tmp[--n];
    }
    o->p[o->len] = '\0';
}

/* 定宽两位：`09:05:03` 才读得懂，`9:5:3` 像坏数据 */
static void sh_put_2(sh_out_t *o, int v)
{
    if (v < 10) {
        sh_puts(o, "0");
    }
    sh_dec(o, v);
}

static void sh_put_x10(sh_out_t *o, int32_t v)
{
    if (v < 0) {
        sh_puts(o, "-");
        v = -v;
    }
    sh_dec(o, (int)(v / 10));
    sh_puts(o, ".");
    sh_dec(o, (int)(v % 10));
}

static int sh_atoi(const char *s, int *out)
{
    int v = 0;
    unsigned digits = 0U;

    if (s == NULL || *s == '\0') {
        return 1;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9' || digits >= 5U) {
            return 1;
        }
        v = v * 10 + (*s - '0');
        digits++;
        s++;
    }
    *out = v;
    return 0;
}

/* ---- 命令 ---------------------------------------------------------------
 *
 * fn 返回 1 = 成功、0 = 执行器本身失败（外设没就绪等）。**不在这里判 PASS**：
 * 判据属于调用方（POST 看返回值，人眼看文字）。 */

typedef struct {
    const char *name;
    const char *usage;
    int (*fn)(int argc, char **argv, sh_out_t *o);
} sh_cmd_t;

static int cmd_help(int argc, char **argv, sh_out_t *o);
static int cmd_uptime(int argc, char **argv, sh_out_t *o);
static int cmd_rtc(int argc, char **argv, sh_out_t *o);
static int cmd_temp(int argc, char **argv, sh_out_t *o);
static int cmd_led(int argc, char **argv, sh_out_t *o);
static int cmd_beep(int argc, char **argv, sh_out_t *o);

static const sh_cmd_t s_cmds[] = {
    {"help", "help", cmd_help},
    {"uptime", "uptime", cmd_uptime},
    {"rtc", "rtc", cmd_rtc},
    {"temp", "temp", cmd_temp},
    {"led", "led <0..2> <0|1>", cmd_led},
    {"beep", "beep <ms, 1..500>", cmd_beep},
};

#define SH_CMD_COUNT (sizeof(s_cmds) / sizeof(s_cmds[0]))

static int cmd_help(int argc, char **argv, sh_out_t *o)
{
    unsigned i;

    (void)argc;
    (void)argv;
    sh_puts(o, "cmds=");
    sh_dec(o, (int)SH_CMD_COUNT);
    sh_puts(o, ":");
    for (i = 0U; i < SH_CMD_COUNT; i++) {
        if (i != 0U) {
            sh_puts(o, ", ");
        }
        sh_puts(o, s_cmds[i].usage);
    }
    return 1;
}

static int cmd_uptime(int argc, char **argv, sh_out_t *o)
{
    (void)argc;
    (void)argv;
    /* osl_tick_now() 是 1 ms 的 tick（configTICK_RATE_HZ = 1000） */
    sh_puts(o, "uptime=");
    sh_dec(o, (int)osl_tick_now());
    sh_puts(o, "ms");
    return 1;
}

static int cmd_rtc(int argc, char **argv, sh_out_t *o)
{
    const bsl_rtc_api_t *api = bsl_devapi_rtc();
    bsl_rtc_time_t t;

    (void)argc;
    (void)argv;
    if (api->ready() == 0) {
        sh_puts(o, "rtc=not-ready");
        return 0;
    }
    if (api->get(&t) != BSL_OK) {
        /* 字段非法与寄存器没同步都归这里：调用方只关心"读不到" */
        sh_puts(o, "rtc=read-err");
        return 0;
    }
    sh_puts(o, "rtc=");
    sh_dec(o, (int)t.year);
    sh_puts(o, "-");
    sh_put_2(o, (int)t.month);
    sh_puts(o, "-");
    sh_put_2(o, (int)t.day);
    sh_puts(o, " ");
    sh_put_2(o, (int)t.hour);
    sh_puts(o, ":");
    sh_put_2(o, (int)t.minute);
    sh_puts(o, ":");
    sh_put_2(o, (int)t.second);
    sh_puts(o, " src=");
    sh_puts(o, (api->clock_source() == BSL_RTC_SRC_LSE) ? "lse" : "lsi");
    return 1;
}

static int cmd_temp(int argc, char **argv, sh_out_t *o)
{
    bsl_adc_temp_t s;

    (void)argc;
    (void)argv;
    if (bsl_adc_temp_init() != BSL_OK) {
        sh_puts(o, "temp=init-err");
        return 0;
    }
    if (bsl_adc_temp_read(&s) != BSL_OK) {
        /* cal_valid = 0 时 celsius_x10 没有意义，只报原始码 —— 打一个"看着像
         * 温度"的假数比不报更误人 */
        sh_puts(o, "temp=inv raw=");
        sh_dec(o, (int)s.raw);
        return 0;
    }
    sh_puts(o, "temp=");
    sh_put_x10(o, s.celsius_x10);
    sh_puts(o, "C raw=");
    sh_dec(o, (int)s.raw);
    return 1;
}

static int cmd_led(int argc, char **argv, sh_out_t *o)
{
    int idx = 0;
    int on = 0;

    if (argc < 3 || sh_atoi(argv[1], &idx) != 0 || sh_atoi(argv[2], &on) != 0
        || idx < 0 || idx > 2 || on < 0 || on > 1) {
        sh_puts(o, "usage: led <0..2> <0|1>");
        return 0;
    }
    bsl_devapi_leds()->set((board_led_id_t)idx, on);
    sh_puts(o, "led ");
    sh_dec(o, idx);
    sh_puts(o, "=");
    sh_dec(o, on);
    return 1;
}

static int cmd_beep(int argc, char **argv, sh_out_t *o)
{
    int ms = 0;

    if (argc < 2 || sh_atoi(argv[1], &ms) != 0 || ms < 1 || ms > 500) {
        sh_puts(o, "usage: beep <ms, 1..500>");
        return 0;
    }
    /* 上限 500 ms：这条执行器在 shell 任务里阻塞，写 5000 会把命令行卡死 5 s，
     * 现场只会觉得"串口死了" */
    bsl_devapi_beep()->on();
    osl_thread_delay_ms((uint32_t)ms);
    bsl_devapi_beep()->off();
    sh_puts(o, "beep ");
    sh_dec(o, ms);
    sh_puts(o, "ms");
    return 1;
}

/* ---- 执行与轮询 --------------------------------------------------------- */

bsl_err_t fml_shell_exec(const char *line, char *out, unsigned cap)
{
    char buf[FML_SHELL_LINE_MAX];
    char *argv[4];
    sh_out_t o;
    unsigned i;
    unsigned n = 0U;
    int argc = 0;
    char *p;

    if (out == NULL || cap < 2U) {
        return BSL_ERR_INVAL;
    }
    o.p = out;
    o.cap = cap;
    o.len = 0U;
    out[0] = '\0';

    if (line == NULL) {
        return BSL_ERR_INVAL;
    }
    while (*line == ' ') {
        line++;
    }
    /* 拷进本地缓冲再切词：不改调用方的字符串（POST 传的是字符串字面量） */
    while ((line[n] != '\0') && ((n + 1U) < FML_SHELL_LINE_MAX)) {
        buf[n] = line[n];
        n++;
    }
    buf[n] = '\0';

    p = buf;
    while ((*p != '\0') && (argc < 4)) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while ((*p != '\0') && (*p != ' ')) {
            p++;
        }
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }
    if (argc == 0) {
        return BSL_ERR_INVAL;
    }

    for (i = 0U; i < SH_CMD_COUNT; i++) {
        if (strcmp(argv[0], s_cmds[i].name) == 0) {
            return (s_cmds[i].fn(argc, argv, &o) != 0) ? BSL_OK : BSL_ERR_NOSUPPORT;
        }
    }
    sh_puts(&o, "unknown '");
    sh_puts(&o, argv[0]);
    sh_puts(&o, "', type help");
    return BSL_ERR_INVAL;
}

bsl_err_t fml_shell_init(void)
{
    /* 没有需要初始化的状态：命令表是 const，行缓冲清空即可。
     * 保留这个入口是为了让 POST 与任务走同一个"准备好了"的语义，
     * 将来若加历史记录/别名，只需改这里。 */
    return BSL_OK;
}

unsigned fml_shell_cmd_count(void)
{
    return (unsigned)SH_CMD_COUNT;
}

static char s_line[FML_SHELL_LINE_MAX];
static unsigned s_len;
static int s_started;

/* 回显 + 结果各占一行。两次 bsl_log_puts 之间可能插进别的任务的日志 ——
 * 那是"两条独立的行"，不会把某一行的字符撕开（bsl_log_puts 单次调用独占 UART）。
 * 回显不能省：大多数串口终端默认关本地回显，不回显等于让用户盲打。 */
void fml_shell_poll(void)
{
    char c;

    while (board_debug_uart_getc(&c) != 0) {
        if ((c == '\r') || (c == '\n')) {
            if (s_len > 0U) {
                char out[FML_SHELL_OUT_MAX];
                char echo[FML_SHELL_LINE_MAX + 16U];
                sh_out_t e = SH_OUT_INIT(echo, (unsigned)sizeof(echo));

                sh_puts(&e, "[shell] > ");
                sh_puts(&e, s_line);
                sh_puts(&e, "\r\n");
                bsl_log_puts(echo);

                (void)fml_shell_exec(s_line, out, (unsigned)sizeof(out));
                bsl_log_puts(out);
                bsl_log_puts("\r\n");
                s_len = 0U;
            }
            continue;
        }
        /* 超长整行丢弃而不是截断后执行：截断会把 `led 1 1xxx` 变成 `led 1 1`，
         * 一条写错的命令却执行成功了，比拒绝更难查。 */
        if ((s_len + 1U) < FML_SHELL_LINE_MAX) {
            s_line[s_len++] = c;
            s_line[s_len] = '\0';
        }
    }
}

static void shell_task(void *arg)
{
    char ready[48];
    sh_out_t o = SH_OUT_INIT(ready, (unsigned)sizeof(ready));

    (void)arg;
    /* 先让 100 ms：启动瞬间 `button` / `app_menu` / 启动横幅会同时抢 UART，
     * 而 bsl_log_puts 是"占住 50 ms 超时后丢行"的语义 —— 实测启动日志里稳定出现
     * `uart busy: 1 line(s) not shown`，丢的正是这一行 ready。延时只影响一行诊断，
     * 不影响 shell 之后每 10 ms 的取字节。 */
    osl_thread_delay_ms(100U);
    sh_puts(&o, "[shell] ready cmds=");
    sh_dec(&o, (int)SH_CMD_COUNT);
    sh_puts(&o, ", type help\r\n");
    bsl_log_puts(ready);
    for (;;) {
        fml_shell_poll();
        osl_thread_delay_ms(10U);
    }
}

int fml_shell_start(void)
{
    if (s_started != 0) {
        return 0;
    }
    if (osl_thread_create(shell_task, "shell", OSL_THREAD_STACK_WORDS_DEFAULT, NULL, 1U) != 0) {
        return 1;
    }
    s_started = 1;
    return 0;
}
