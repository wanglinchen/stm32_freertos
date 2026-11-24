#include "fml_audio.h"
#include "fml_shell.h"

#include <string.h>

#include "board_debug_uart.h"
#include "board_device_registry.h"
#include "bsl_adc.h"
#include "bsl_i2c_gpio.h"
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

/* 两位小写十六进制。地址与寄存器一律按 hex 输出：`0x10` 与十进制 `16` 混着看
 * 是排查时最容易读错的一处（数据手册整本都是 hex）。 */
static void sh_put_hex2(sh_out_t *o, int v)
{
    static const char s_digits[] = "0123456789abcdef";
    char two[3];

    if (v < 0) {
        v = 0;
    }
    if (v > 0xFF) {
        v = 0xFF;
    }
    two[0] = s_digits[(v >> 4) & 0x0F];
    two[1] = s_digits[v & 0x0F];
    two[2] = '\0';
    sh_puts(o, two);
}

/* 解析 1~2 位十六进制，允许 `0x` / `0X` 前缀。范围校验由调用方做：
 * 0x7F 与 0xFF 在同一个解析器里都能过，谁是地址谁是数据由用途决定。 */
static int sh_hex(const char *s, int *out)
{
    unsigned v = 0U;
    unsigned digits = 0U;

    if (s == NULL) {
        return 1;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    if (*s == '\0') {
        return 1;
    }
    while (*s != '\0') {
        int d;
        char c = *s;

        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return 1;
        }
        if (digits >= 2U) {
            return 1;
        }
        v = (v << 4) | (unsigned)d;
        digits++;
        s++;
    }
    *out = (int)v;
    return 0;
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
static int cmd_i2c(int argc, char **argv, sh_out_t *o);
static int cmd_tone(int argc, char **argv, sh_out_t *o);
static int cmd_vol(int argc, char **argv, sh_out_t *o);

static const sh_cmd_t s_cmds[] = {
    {"help", "help", cmd_help},
    {"uptime", "uptime", cmd_uptime},
    {"rtc", "rtc", cmd_rtc},
    {"temp", "temp", cmd_temp},
    {"led", "led <0..2> <0|1>", cmd_led},
    {"i2c", "i2c scan | i2c rd <a7> <r8>", cmd_i2c},
    {"tone", "tone <hz 20..20000> <ms 1..5000>", cmd_tone},
    {"vol", "vol <0..63>", cmd_vol},
};

/* `beep` 命令已删除：蜂鸣器由策略永久禁用（bsl/board_beep.c 的 BEEP_ALLOW_ON），
 * 留一条"能执行但什么都不响"的命令只会让人以为驱动坏了。 */

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

/* 主 I2C 总线的诊断入口（只读）。
 *
 * 存在的理由：P6c 的音频 codec 型号在两张纸上对不上 —— pinmap 写 ES8388 在
 * PH4/PH5，而早期扫描的应答清单里有 0x1A（WM8978 的地址）却没有 0x10。
 * 这种矛盾只能靠"谁应答"来结，不能靠再读一遍文档。
 *
 * 只读不写：`i2c rd` 只发 1 字节寄存器地址（8 位寻址）再读 1 字节；没有写寄存器
 * 的子命令 —— 排查阶段写 codec 寄存器拿不到任何信息，写错还会把功放拉成静音。 */
static int cmd_i2c(int argc, char **argv, sh_out_t *o)
{
    const bsl_i2c_gpio_bus_t *bus;
    int a = 0;
    int r = 0;
    uint8_t v = 0U;

    if (bsl_board_i2c_gpio_ready() == 0U) {
        sh_puts(o, "i2c=not-ready");
        return 0;
    }
    bus = bsl_board_i2c_gpio_bus();
    if ((argc >= 2) && (strcmp(argv[1], "scan") == 0)) {
        unsigned n = 0U;

        /* 0x08 以下与 0x78 以上是 I2C 保留地址段（广播、10 位地址前缀等），
         * 扫进去只会出假应答 */
        sh_puts(o, "i2c scan:");
        for (a = 0x08; a <= 0x77; a++) {
            if (bsl_i2c_gpio_probe(bus, (uint8_t)a) == BSL_OK) {
                sh_puts(o, " ");
                sh_put_hex2(o, a);
                n++;
            }
        }
        sh_puts(o, " n=");
        sh_dec(o, (int)n);
        return 1;
    }
    if ((argc >= 4) && (strcmp(argv[1], "rd") == 0)) {
        if ((sh_hex(argv[2], &a) != 0) || (sh_hex(argv[3], &r) != 0)
            || (a < 0x08) || (a > 0x77) || (r < 0) || (r > 0xFF)) {
            sh_puts(o, "usage: i2c rd <a7 08..77> <r8 00..ff>");
            return 0;
        }
        sh_puts(o, "i2c ");
        sh_put_hex2(o, a);
        sh_puts(o, " r");
        sh_put_hex2(o, r);
        if (bsl_i2c_gpio_read_reg8(bus, (uint8_t)a, (uint8_t)r, &v, 1U) != BSL_OK) {
            /* NACK 与总线错合并成一句：调用方要的是"这个地址读不出来" */
            sh_puts(o, "=nack");
            return 0;
        }
        sh_puts(o, "=");
        sh_put_hex2(o, (int)v);
        return 1;
    }
    sh_puts(o, "usage: i2c scan | i2c rd <a7> <r8>");
    return 0;
}

/* tone：阻塞播放，响完才返回。所以这一行 `tone ok` 打出来的时候声音已经结束了
 * —— 人耳听到的是**上一条命令**的回响，不是"正在响"。要边响边看串口做不到，
 * 串口本身是独占的（bsl_log_puts 有 s_uart_busy）。 */
static int cmd_tone(int argc, char **argv, sh_out_t *o)
{
    int hz = 0;
    int ms = 0;

    if ((argc < 3) || (sh_atoi(argv[1], &hz) != 0) || (sh_atoi(argv[2], &ms) != 0)
        || (hz < 20) || (hz > 20000) || (ms < 1) || (ms > 5000)) {
        sh_puts(o, "usage: tone <hz 20..20000> <ms 1..5000>");
        return 0;
    }
    if (fml_audio_ready() == 0) {
        sh_puts(o, "tone=not-ready");
        return 0;
    }
    sh_puts(o, "tone ");
    sh_dec(o, hz);
    sh_puts(o, "Hz ");
    sh_dec(o, ms);
    sh_puts(o, "ms ... ");
    if (fml_audio_tone((unsigned)hz, (unsigned)ms) != BSL_OK) {
        sh_puts(o, "FAIL");
        return 0;
    }
    sh_puts(o, "ok");
    return 1;
}

static int cmd_vol(int argc, char **argv, sh_out_t *o)
{
    int v = 0;

    if ((argc < 2) || (sh_atoi(argv[1], &v) != 0) || (v < 0) || (v > 63)) {
        sh_puts(o, "usage: vol <0..63>");
        return 0;
    }
    if (fml_audio_ready() == 0) {
        sh_puts(o, "vol=not-ready");
        return 0;
    }
    if (fml_audio_set_volume((unsigned)v) != BSL_OK) {
        sh_puts(o, "vol FAIL");
        return 0;
    }
    sh_puts(o, "vol=");
    sh_dec(o, v);
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
