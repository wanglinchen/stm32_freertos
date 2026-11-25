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

/* 定宽十六进制由已有的 sh_put_hex2 提供（P6c 的 i2c 命令就靠它打 a7/r8）。
 * 这里不再另写一份 —— 同一件事两种写法，改一处忘一处就会打出位宽不一致的值。 */

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
static int sh_hex_max(const char *s, int *out, unsigned maxd)
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
        if (digits >= maxd) {
            return 1;
        }
        v = (v << 4) | (unsigned)d;
        digits++;
        s++;
    }
    *out = (int)v;
    return 0;
}

/* 1~2 位十六进制的常用形态（寄存器、字节）。三位的只有 CAN 标准帧 ID（最大 0x7FF），
 * 由 cmd_can 走 sh_hex_max(..., 3)。 */
static int sh_hex(const char *s, int *out)
{
    return sh_hex_max(s, out, 2U);
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
static int cmd_als(int argc, char **argv, sh_out_t *o);
static int cmd_imu(int argc, char **argv, sh_out_t *o);
static int cmd_eeprom(int argc, char **argv, sh_out_t *o);
static int cmd_tpad(int argc, char **argv, sh_out_t *o);
static int cmd_can(int argc, char **argv, sh_out_t *o);
static int cmd_uart(int argc, char **argv, sh_out_t *o);

static const sh_cmd_t s_cmds[] = {
    {"help", "help", cmd_help},
    {"uptime", "uptime", cmd_uptime},
    {"rtc", "rtc", cmd_rtc},
    {"temp", "temp", cmd_temp},
    {"led", "led <0..2> <0|1>", cmd_led},
    {"i2c", "i2c scan | i2c rd <a7> <r8>", cmd_i2c},
    {"tone", "tone <hz 20..20000> <ms 1..5000>", cmd_tone},
    {"vol", "vol <0..63>", cmd_vol},
    {"als", "als", cmd_als},
    {"imu", "imu", cmd_imu},
    {"eeprom", "eeprom <addr8 0..255>", cmd_eeprom},
    {"tpad", "tpad", cmd_tpad},
    {"can", "can <id 0..0x7ff>", cmd_can},
    {"uart", "uart <hex8>", cmd_uart},
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

/* als / imu / eeprom / tpad（P6d）：四条**只读**命令，充当四个驱动的真消费者。
 *
 * 为什么必须有它们：`check` 的 G 组会把"头里声明、镜像里没符号"的接口判成假 API，
 * 而 `--gc-sections` 会把"只有 POST 调一次"的函数也留下 —— 但那仍然只是自检路径；
 * 串口命令是"运行期可达"的第二条路径，也让人能现场复验（不必重新烧机看 POST）。
 *
 * `eeprom` 刻意**只读不写**：写 EEPROM 是持久副作用，命令行里不该有这条通道；
 * 写-读-还原只发生在 POST 自检里，且必须还原。 */
static int cmd_als(int argc, char **argv, sh_out_t *o)
{
    uint16_t ir = 0U;
    uint16_t ps = 0U;
    uint16_t als_v = 0U;

    (void)argc;
    (void)argv;
    if (bsl_devapi_als()->ready() == 0) {
        sh_puts(o, "als=not-ready");
        return 0;
    }
    if (bsl_devapi_als()->read(&ir, &ps, &als_v) != BSL_OK) {
        sh_puts(o, "als FAIL");
        return 0;
    }
    sh_puts(o, "ir=");
    sh_dec(o, (int)ir);
    sh_puts(o, " ps=");
    sh_dec(o, (int)ps);
    sh_puts(o, " als=");
    sh_dec(o, (int)als_v);
    return 1;
}

static int cmd_imu(int argc, char **argv, sh_out_t *o)
{
    int16_t acc[3];
    int16_t gyr[3];

    (void)argc;
    (void)argv;
    if (bsl_devapi_imu()->ready() == 0) {
        sh_puts(o, "imu=not-ready");
        return 0;
    }
    if (bsl_devapi_imu()->read(acc, gyr) != BSL_OK) {
        sh_puts(o, "imu FAIL");
        return 0;
    }
    sh_puts(o, "who=");
    sh_put_hex2(o, (int)bsl_devapi_imu()->who());
    sh_puts(o, " mag=");
    sh_put_hex2(o, (int)bsl_devapi_imu()->mag_wia());
    sh_puts(o, " acc=");
    sh_dec(o, acc[0]);
    sh_puts(o, ",");
    sh_dec(o, acc[1]);
    sh_puts(o, ",");
    sh_dec(o, acc[2]);
    sh_puts(o, " gyr=");
    sh_dec(o, gyr[0]);
    sh_puts(o, ",");
    sh_dec(o, gyr[1]);
    sh_puts(o, ",");
    sh_dec(o, gyr[2]);
    return 1;
}

static int cmd_eeprom(int argc, char **argv, sh_out_t *o)
{
    int addr = 0;
    uint8_t v = 0U;

    if ((argc < 2) || (sh_atoi(argv[1], &addr) != 0) || (addr < 0) || (addr > 255)) {
        sh_puts(o, "usage: eeprom <addr8 0..255>");
        return 0;
    }
    if (bsl_devapi_eeprom()->ready() == 0) {
        sh_puts(o, "eeprom=not-ready");
        return 0;
    }
    if (bsl_devapi_eeprom()->read((uint8_t)addr, &v, 1U) != BSL_OK) {
        sh_puts(o, "eeprom FAIL");
        return 0;
    }
    sh_puts(o, "eeprom[");
    sh_dec(o, addr);
    sh_puts(o, "]=");
    sh_put_hex2(o, (int)v);
    return 1;
}

/* tpad：报实时计数值与判定结果。"按一下"要人手，所以这条命令的设计意图是
 * HIL 里盯着读数按下去 —— 打印原始值比只打"按下/没按下"更能定位灵敏度问题。 */
static int cmd_tpad(int argc, char **argv, sh_out_t *o)
{
    int hit;
    uint16_t base;

    (void)argc;
    (void)argv;
    if (bsl_devapi_tpad()->ready() == 0) {
        sh_puts(o, "tpad=not-ready");
        return 0;
    }
    base = bsl_devapi_tpad()->baseline();
    hit = bsl_devapi_tpad()->scan(0);
    sh_puts(o, "default=");
    sh_dec(o, (int)base);
    sh_puts(o, " thr=");
    sh_dec(o, (int)bsl_devapi_tpad()->threshold());
    sh_puts(o, (hit == 1) ? " pressed" : " idle");
    return 1;
}

/* can：回环自发自收。回环模式下 CAN 核把 Tx 直接送进 Rx，所以这条命令不需要
 * 第二个 CAN 节点、也不依赖 P9 跳线 —— 敲下去能看到自己发出的同一帧才算通。
 * 打印发送结果、收到的 ID/长度/8 字节，以及 BTR 的 brp/tseg（位率偏了先看这两个数）。 */
static int cmd_can(int argc, char **argv, sh_out_t *o)
{
    static const uint8_t tx[8] = {0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xABU, 0xCDU, 0xEFU};
    uint8_t rx[8];
    uint8_t rxlen = 0U;
    uint32_t rxid = 0U;
    uint32_t brp = 0U;
    uint32_t tseg = 0U;
    int id = 0;
    int i;

    if ((argc < 2) || (sh_hex_max(argv[1], &id, 3U) != 0) || (id < 0) || (id > 0x7FF)) {
        sh_puts(o, "usage: can <id 0..0x7ff>");
        return 0;
    }
    if (bsl_devapi_can()->ready() == 0) {
        sh_puts(o, "can=not-ready");
        return 0;
    }
    bsl_devapi_can()->timing(&brp, &tseg);
    sh_puts(o, "can bt=");
    sh_dec(o, (int)bsl_devapi_can()->bit_rate());
    sh_puts(o, " brp=");
    sh_dec(o, (int)brp);
    sh_puts(o, " tseg=");
    sh_dec(o, (int)tseg);
    if (bsl_devapi_can()->send((uint32_t)id, tx, 8U) != BSL_OK) {
        sh_puts(o, " send FAIL");
        return 0;
    }
    for (i = 0; i < 20; i++) {
        if (bsl_devapi_can()->recv(&rxid, rx, &rxlen) != 0) {
            break;
        }
        osl_thread_delay_ms(1U);
    }
    sh_puts(o, " rx id=");
    sh_put_hex2(o, (int)((rxid >> 8) & 0xFFU));
    sh_put_hex2(o, (int)(rxid & 0xFFU));
    sh_puts(o, " len=");
    sh_dec(o, (int)rxlen);
    sh_puts(o, " data=");
    for (i = 0; i < (int)rxlen; i++) {
        sh_put_hex2(o, (int)rx[i]);
    }
    return (rxlen == 8U && rxid == (uint32_t)id) ? 1 : 0;
}

/* uart：往 USART3 发一个字节并报实测耗时。8N1 一帧 10 bit，115200 下 86.8 µs ——
 * 读数成比例偏就是 BRR 按错时钟算了。收帧要等对端（本板没接），所以这条命令
 * 只做发送侧；帧状态机由 POST 的 usart 项用注入自证。 */
static int cmd_uart(int argc, char **argv, sh_out_t *o)
{
    int b = 0;
    uint32_t us = 0U;
    bsl_err_t rc;

    if ((argc < 2) || (sh_hex(argv[1], &b) != 0) || (b < 0) || (b > 255)) {
        sh_puts(o, "usage: uart <hex8>");
        return 0;
    }
    if (bsl_devapi_usart3()->ready() == 0) {
        sh_puts(o, "usart3=not-ready");
        return 0;
    }
    rc = bsl_devapi_usart3()->send_byte((uint8_t)b, &us);
    if (rc != BSL_OK) {
        sh_puts(o, "usart3 send FAIL rc=");
        sh_dec(o, (int)rc);
        return 0;
    }
    sh_puts(o, "usart3 baud=");
    sh_dec(o, (int)bsl_devapi_usart3()->baud());
    sh_puts(o, " brr=");
    sh_dec(o, (int)bsl_devapi_usart3()->brr());
    sh_puts(o, " tx_us=");
    sh_dec(o, (int)us);
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
