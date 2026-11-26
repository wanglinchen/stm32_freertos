#include "app.h"

#include <string.h>

#include "board_lcd.h"
#include "bsl_log.h"
#include "fml_audio.h"
#include "fml_fatfs.h"
#include "fml_font.h"
#include "fml_gui.h"
#include "fml_ui.h"

/*
 * 音乐播放器（P9e-2）：把参考工程 `audioplay.c` + `AUDIOCODEC/wav/wavplay.c`
 * 那条"wav 播放"的路走通，界面壳按本工程的 fml_ui 约定重写。
 *
 * 三条要说清楚的事：
 *
 * 1) **采样率不跟文件走**。`bsl_sai` 的帧率由 PLLI2S → MCLK/256 定死 47973 Hz，
 *    F4 的 SAI 没有分数分频（44.1k 在 49.125 MHz 下除不尽）。所以 fml_audio 把
 *    任何素材线性插值到 47973 Hz —— 音调、时长都是对的，高采样率素材有轻微插值
 *    失真。这是本工程的边界，不在界面上吹"支持 44.1 kHz"。
 *
 * 2) **素材两级**：卷里第一个 `.WAV`（根目录 → 一层子目录，与看图器/电子书同一套
 *    扫描；判扩展名只看短名，取路径用长名）→ 没有就用构建期内置样本
 *    （`build/gen_wav_sample.py` 生成的 8 kHz / 单声道 / 16 位 / 0.6 s 三音阶）。
 *    状态行与 POST 都如实打 `src=file` / `src=sample`，不把内置样本说成"卡里的歌"。
 *
 * 3) **块间接缝**。播放走 `bsl_sai_play_blocking`（2048 帧 ≈ 42.7 ms 一块，块尾
 *    停一次 SAI 再起下一块），所以每 42.7 ms 有一个极短的缝。不装双缓冲 DMA 回调
 *    是有意的：那是 bsl_sai 的第二个重大改动（环形 DMA + 半传输中断），留到需要
 *    "连续播放不咔"的场景再立批。
 *
 * 只播 wav（PCM / extensible）。mp3/flac/ape 是三个外部解码库，本批不做（见
 * 对照表 P9e-2 行的"明确不做"）。
 */
#define MUSIC_LOG_MOD "app_music"

#define MUSIC_PATH_MAX 96U
#define MUSIC_MARGIN 16U
#define MUSIC_BTN_W 84U
#define MUSIC_BTN_H 40U
#define MUSIC_INFO_X MUSIC_MARGIN
#define MUSIC_INFO_Y 44U
#define MUSIC_INFO_LINE 26U
#define MUSIC_INFO_LINES 6U

#define MUSIC_ID_PLAY 0x01U
#define MUSIC_ID_STOP 0x02U
#define MUSIC_ID_RESTART 0x03U

/* POST 只播两块（2 × 2048 帧 ≈ 85.4 ms）：够抓"真的从 DMA 走完 + 时钟没跑偏"，
 * 又不至于让自检多等半秒。 */
#define MUSIC_POST_BLOCKS 2U
#define MUSIC_POST_MIN_FRAMES (2U * 2048U)

static fml_ui_win_t s_win;
static int s_built;
static char s_path[MUSIC_PATH_MAX];
static fml_audio_wav_t s_info;
static int s_loaded;
static int s_playing;
static uint32_t s_out_total;        /* 重采样后的总输出帧数 */
static uint32_t s_played_out;       /* 已发出的输出帧 */
static uint32_t s_elapsed_us;
static unsigned s_blocks;
static int s_last_rc;

/* 目录项放模块级 static：app_menu 的任务栈只有 1 KB 级，16 项 × 52 B 会顶到边界
 * （与看图器 / 电子书同一理由）。只有 app_menu 一个任务会调用这里。 */
static fml_fatfs_dirent_t s_de[16];

/* 把 v 的十进制写进 dst（cap 按字节算），返回写入的字符数。 */
static unsigned music_put_u32(char *dst, unsigned cap, uint32_t v)
{
    char tmp[12];
    unsigned n = 0U;
    unsigned i;

    do {
        tmp[n++] = (char)('0' + (int)(v % 10U));
        v /= 10U;
    } while ((v != 0U) && (n < sizeof(tmp)));
    for (i = 0U; (i < n) && ((i + 1U) < cap); i++) {
        dst[i] = tmp[n - 1U - i];
    }
    if (cap > 0U) {
        dst[(i < cap) ? i : (cap - 1U)] = '\0';
    }
    return i;
}

/* 追加固定串，返回新的 pos（便于接着拼）。 */
static unsigned music_put_str(char *dst, unsigned cap, unsigned pos, const char *s)
{
    unsigned i;

    for (i = 0U; (s[i] != '\0') && ((pos + 1U) < cap); i++) {
        dst[pos++] = s[i];
    }
    dst[(pos < cap) ? pos : (cap - 1U)] = '\0';
    return pos;
}

static unsigned music_put_num(char *dst, unsigned cap, unsigned pos, uint32_t v)
{
    unsigned n;

    if ((pos + 1U) >= cap) {
        return pos;
    }
    n = music_put_u32(&dst[pos], cap - pos, v);
    return pos + n;
}

static int music_ext_wav(const char *sname)
{
    unsigned n = 0U;

    while ((sname[n] != '\0') && (n < FML_FATFS_SNAME_LEN)) {
        n++;
    }
    if (n < 4U) {
        return 0;
    }
    if (sname[n - 4U] != '.') {
        return 0;
    }
    /* 三位逐位比大小写：FatFs 短名默认大写，但卷也可能是别的小写命名工具格出来的。 */
    return (((sname[n - 3U] == 'W') || (sname[n - 3U] == 'w'))
            && ((sname[n - 2U] == 'A') || (sname[n - 2U] == 'a'))
            && ((sname[n - 1U] == 'V') || (sname[n - 1U] == 'v'))) ? 1 : 0;
}

static int music_join(char *dst, unsigned cap, const char *dir, const char *name)
{
    unsigned n = 0U;
    unsigned i;

    for (i = 0U; (dir[i] != '\0') && ((n + 1U) < cap); i++) {
        dst[n++] = dir[i];
    }
    if ((n > 0U) && (dst[n - 1U] != '/')) {
        if ((n + 1U) >= cap) {
            return 1;
        }
        dst[n++] = '/';
    }
    for (i = 0U; (name[i] != '\0') && ((n + 1U) < cap); i++) {
        dst[n++] = name[i];
    }
    dst[n] = '\0';
    return (name[i] != '\0') ? 1 : 0;
}

static int music_find_in(const char *dir, char *out, unsigned cap)
{
    unsigned n = 0U;
    unsigned i;

    if (fml_fatfs_list_dir(dir, s_de, 16U, &n) != BSL_OK) {
        return 0;
    }
    for (i = 0U; (i < 16U) && (i < n); i++) {
        if ((s_de[i].is_dir != 0U) || (music_ext_wav(s_de[i].sname) == 0)) {
            continue;
        }
        if (music_join(out, cap, dir, s_de[i].name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int music_find(char *out, unsigned cap)
{
    fml_fatfs_dirent_t root[4];
    unsigned n = 0U;
    unsigned i;

    if (fml_fatfs_list_dir("/", root, 4U, &n) != BSL_OK) {
        return 0;
    }
    for (i = 0U; (i < 4U) && (i < n); i++) {
        if ((root[i].is_dir == 0U) && (music_ext_wav(root[i].sname) != 0)) {
            if (music_join(out, cap, "/", root[i].name) == 0) {
                return 1;
            }
        }
    }
    for (i = 0U; (i < 4U) && (i < n); i++) {
        char sub[FML_FATFS_LNAME_LEN + 2U];

        if (root[i].is_dir == 0U) {
            continue;
        }
        if (music_join(sub, (unsigned)sizeof(sub), "/", root[i].name) != 0) {
            continue;
        }
        if (music_find_in(sub, out, cap) != 0) {
            return 1;
        }
    }
    return 0;
}

/* 打开素材：先卷内 .WAV，失败退内置样本。返回 1 = 装好（s_info 有效）。 */
static int music_load(void)
{
    bsl_err_t rc;

    (void)fml_audio_wav_close();
    s_loaded = 0;
    s_path[0] = '\0';
    if ((fml_fatfs_mounted() != 0) && (music_find(s_path, MUSIC_PATH_MAX) != 0)) {
        rc = fml_audio_wav_open(s_path, &s_info);
        if (rc == BSL_OK) {
            s_loaded = 1;
            s_out_total = (uint32_t)(((uint64_t)s_info.frames * fml_audio_sample_rate())
                                     / s_info.rate);
            return 1;
        }
        BSL_LOG_MOD_INF(MUSIC_LOG_MOD, "open file rc=");
        bsl_log_put_dec((int)rc);
        BSL_LOG_INF("\r\n");
    }
    rc = fml_audio_wav_open(NULL, &s_info);
    if (rc != BSL_OK) {
        BSL_LOG_MOD_INF(MUSIC_LOG_MOD, "open sample rc=");
        bsl_log_put_dec((int)rc);
        BSL_LOG_INF("\r\n");
        return 0;
    }
    s_loaded = 1;
    s_out_total = (uint32_t)(((uint64_t)s_info.frames * fml_audio_sample_rate()) / s_info.rate);
    return 1;
}

/* 信息区 = 6 行 × 26 px。整块先按窗口底色抹掉再画，避免叠字。 */
static void music_draw_info(void)
{
    char line[72];
    unsigned pos;
    uint32_t y = MUSIC_INFO_Y;

    fml_gui_fill(0U, MUSIC_INFO_Y, s_win.w, MUSIC_INFO_LINES * MUSIC_INFO_LINE, s_win.bg);

    if (s_loaded == 0) {
        (void)fml_gui_text(MUSIC_INFO_X, y, "no wav: open failed", &fml_font_ascii8x16,
                           0xF800U, s_win.bg);
        return;
    }

    pos = music_put_str(line, sizeof(line), 0U, (s_info.from_file != 0) ? "src=file " : "src=sample ");
    pos = music_put_num(line, sizeof(line), pos, s_info.rate);
    pos = music_put_str(line, sizeof(line), pos, "Hz ");
    pos = music_put_num(line, sizeof(line), pos, s_info.channels);
    pos = music_put_str(line, sizeof(line), pos, "ch ");
    pos = music_put_num(line, sizeof(line), pos, s_info.bits);
    (void)music_put_str(line, sizeof(line), pos, "bit");
    (void)fml_gui_text(MUSIC_INFO_X, y, line, &fml_font_ascii8x16, 0x07E0U, s_win.bg);
    y += MUSIC_INFO_LINE;

    pos = music_put_str(line, sizeof(line), 0U, "frames=");
    pos = music_put_num(line, sizeof(line), pos, s_info.frames);
    pos = music_put_str(line, sizeof(line), pos, " total=");
    pos = music_put_num(line, sizeof(line), pos, s_info.ms_total);
    (void)music_put_str(line, sizeof(line), pos, "ms");
    (void)fml_gui_text(MUSIC_INFO_X, y, line, &fml_font_ascii8x16, 0x07E0U, s_win.bg);
    y += MUSIC_INFO_LINE;

    pos = music_put_str(line, sizeof(line), 0U, "sai fs=");
    pos = music_put_num(line, sizeof(line), pos, fml_audio_sample_rate());
    pos = music_put_str(line, sizeof(line), pos, "Hz out=");
    (void)music_put_num(line, sizeof(line), pos, s_out_total);
    (void)fml_gui_text(MUSIC_INFO_X, y, line, &fml_font_ascii8x16, 0x7FFFU, s_win.bg);
    y += MUSIC_INFO_LINE;

    pos = music_put_str(line, sizeof(line), 0U, "played=");
    pos = music_put_num(line, sizeof(line), pos, s_played_out);
    pos = music_put_str(line, sizeof(line), pos, " blocks=");
    (void)music_put_num(line, sizeof(line), pos, s_blocks);
    (void)fml_gui_text(MUSIC_INFO_X, y, line, &fml_font_ascii8x16, 0xFFE0U, s_win.bg);
    y += MUSIC_INFO_LINE;

    pos = music_put_str(line, sizeof(line), 0U, "state=");
    pos = music_put_str(line, sizeof(line), pos,
                        (s_playing != 0) ? "playing" : ((s_last_rc != 0) ? "error" : "idle"));
    if (s_last_rc != 0) {
        pos = music_put_str(line, sizeof(line), pos, " rc=");
        pos = music_put_num(line, sizeof(line), pos, (uint32_t)(-s_last_rc));
    }
    pos = music_put_str(line, sizeof(line), pos, " ms=");
    (void)music_put_num(line, sizeof(line), pos, s_elapsed_us / 1000U);
    (void)fml_gui_text(MUSIC_INFO_X, y, line, &fml_font_ascii8x16,
                       (s_playing != 0) ? 0x07E0U : 0xFFFFU, s_win.bg);
}

static void music_draw(void)
{
    fml_ui_draw(&s_win);
    music_draw_info();
}

static void music_enter(void)
{
    uint32_t bottom;

    if (s_built == 0) {
        fml_ui_win_init(&s_win, 0U, 0U, board_lcd_width(), board_lcd_height(),
                        "MUSIC - wav (resampled to SAI fs)");
        bottom = board_lcd_height() - 56U;
        (void)fml_ui_btn_add(&s_win, MUSIC_ID_PLAY, "PLAY", MUSIC_MARGIN, bottom,
                             MUSIC_BTN_W, MUSIC_BTN_H);
        (void)fml_ui_btn_add(&s_win, MUSIC_ID_STOP, "STOP",
                             MUSIC_MARGIN + MUSIC_BTN_W + 8U, bottom, MUSIC_BTN_W, MUSIC_BTN_H);
        (void)fml_ui_btn_add(&s_win, MUSIC_ID_RESTART, "REPLAY",
                             MUSIC_MARGIN + (2U * (MUSIC_BTN_W + 8U)), bottom, MUSIC_BTN_W,
                             MUSIC_BTN_H);
        (void)fml_ui_btn_add(&s_win, APP_ID_BACK, "BACK",
                             board_lcd_width() - MUSIC_BTN_W - 16U, 16U, MUSIC_BTN_W,
                             MUSIC_BTN_H);
        s_built = 1;
    }
    s_playing = 0;
    s_played_out = 0U;
    s_elapsed_us = 0U;
    s_blocks = 0U;
    s_last_rc = 0;
    (void)music_load();
    music_draw();
}

/* 每次 tick 播一块（42.7 ms）。不给"一口气播完"：那样 UI 一卡几秒，
 * 触摸事件会堆在队列里，看起来像死机。 */
static void music_tick(void)
{
    uint32_t played = 0U;
    uint32_t us = 0U;
    int rc;

    if ((s_playing == 0) || (s_loaded == 0)) {
        return;
    }
    rc = (int)fml_audio_wav_play(1U, &played, &us);
    if (rc != (int)BSL_OK) {
        s_playing = 0;
        s_last_rc = rc;
        music_draw_info();
        return;
    }
    s_played_out += played;
    s_elapsed_us += us;
    s_blocks++;
    if (played < FML_AUDIO_BLOCK_FRAMES) {
        /* 到素材尾：停止。下一次 PLAY 会先 rewind。 */
        s_playing = 0;
    }
    music_draw_info();
}

static void music_event(const fml_ui_evt_t *ev)
{
    if (ev->type != FML_UI_EVT_CLICK) {
        return;
    }
    if (ev->id == MUSIC_ID_PLAY) {
        if (s_loaded == 0) {
            (void)music_load();
            if (s_loaded == 0) {
                music_draw_info();
                return;
            }
        }
        if (s_playing != 0) {
            return;
        }
        if (s_played_out >= s_out_total) {
            (void)fml_audio_wav_rewind();
            s_played_out = 0U;
            s_elapsed_us = 0U;
            s_blocks = 0U;
        }
        s_last_rc = 0;
        s_playing = 1;
    } else if (ev->id == MUSIC_ID_STOP) {
        s_playing = 0;
        (void)fml_audio_stop();
    } else if (ev->id == MUSIC_ID_RESTART) {
        if (s_loaded != 0) {
            (void)fml_audio_wav_rewind();
            s_played_out = 0U;
            s_elapsed_us = 0U;
            s_blocks = 0U;
            s_last_rc = 0;
            s_playing = 1;
        }
    }
    music_draw_info();
}

static void music_leave(void)
{
    s_playing = 0;
    (void)fml_audio_stop();
    (void)fml_audio_wav_close();
    s_loaded = 0;
    s_played_out = 0U;
    s_elapsed_us = 0U;
    s_blocks = 0U;
}

static fml_ui_win_t *music_win(void)
{
    return &s_win;
}

static const app_desc_t s_ops = {
    .id = 14U,
    .name = "MUSIC",
    .enter = music_enter,
    .tick = music_tick,
    .win = music_win,
    .event = music_event,
    .leave = music_leave,   /* 退出要停 SAI、关文件句柄：这里真占执行器 */
};

const app_desc_t *app_music_ops(void)
{
    return &s_ops;
}

/* POST 用：单独走一遍"打开 → 播两块"，不依赖 app_menu 的 tick 节奏。
 * 判据写在这里而不是 main.c：时序容差用的是这一层的知识（fs 与要发的帧数）。 */
int app_music_selftest(app_music_report_t *rep)
{
    uint32_t played = 0U;
    uint32_t us = 0U;
    uint32_t out_total;
    uint32_t expect_us;
    unsigned b;
    int rc;

    if (rep == NULL) {
        return 0;
    }
    memset(rep, 0, sizeof(*rep));
    if (music_load() == 0) {
        rep->err = (int)BSL_ERR_NOINIT;
        return 0;
    }
    rep->rate = s_info.rate;
    rep->channels = (uint32_t)s_info.channels;
    rep->bits = (uint32_t)s_info.bits;
    rep->frames = s_info.frames;
    rep->ms_total = s_info.ms_total;
    rep->from_file = s_info.from_file;

    for (b = 0U; b < MUSIC_POST_BLOCKS; b++) {
        uint32_t p = 0U;
        uint32_t u = 0U;

        rc = (int)fml_audio_wav_play(1U, &p, &u);
        if (rc != (int)BSL_OK) {
            rep->err = rc;
            break;
        }
        played += p;
        us += u;
        if (p < FML_AUDIO_BLOCK_FRAMES) {
            break;
        }
    }
    rep->played = played;
    rep->elapsed_us = us;
    out_total = s_out_total;
    (void)fml_audio_wav_close();
    s_loaded = 0;

    /* 判据：参数合法 + 两块真的发完 + SAI 没报错 + 数据流时长与 fs 对得上。 */
    expect_us = (uint32_t)(((uint64_t)played * 1000000U) / fml_audio_sample_rate());
    rep->ok = 1;
    if (rep->err != 0) {
        rep->ok = 0;
    }
    if (rep->rate < 4000U || rep->rate > 96000U) {
        rep->ok = 0;
    }
    if (rep->channels < 1U || rep->channels > 2U) {
        rep->ok = 0;
    }
    if (rep->bits != 16U && rep->bits != 24U && rep->bits != 32U) {
        rep->ok = 0;
    }
    if (rep->frames == 0U || out_total == 0U) {
        rep->ok = 0;
    }
    if (played < MUSIC_POST_MIN_FRAMES) {
        rep->ok = 0;
    }
    if (us == 0U) {
        rep->ok = 0;
    }
    /* ±5%：42.7 ms 一块，1 ms 量化就吃掉 2%，所以判据用 us。 */
    if ((us + 1U) * 100U < expect_us * 95U) {
        rep->ok = 0;
    }
    if ((us + 1U) * 100U > expect_us * 105U) {
        rep->ok = 0;
    }
    return rep->ok;
}
