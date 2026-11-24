#include "fml_t9.h"

/*
 * 词库由 fml/tools/gen_pinyin.py 生成后放在 fml_t9_table.c（同一层）。
 * 这里只 extern 四张表，不内联数据 —— 生成物与手写代码分开，重生成不会碰逻辑。
 *
 * 顺手记一个事实：`zhong` 组的首字是 **"中"（0xD6D0）**（组内按 GBK 升序，
 * 而"中"在一级汉字里 0xD6D0 恰好是该组最小）。POST 的 `note=` 判据就靠这条：
 * 按 94664（z-h-o-n-g）选首字，必须得到 0xD6D0。换个词库生成结果不同时，
 * 判据会当场失败 —— 那正是"词库换了却没人发现"该有的样子。
 */
#define T9_SYL_COUNT 396U
#define T9_HAN_COUNT 3755U

extern const char *const fml_t9_syl[T9_SYL_COUNT];
extern const uint16_t fml_t9_syl_first[T9_SYL_COUNT];
extern const uint8_t fml_t9_syl_count[T9_SYL_COUNT];
extern const uint16_t fml_t9_han[T9_HAN_COUNT];

char fml_t9_digit_of(char letter)
{
    if ((letter >= 'a') && (letter <= 'c')) {
        return '2';
    }
    if ((letter >= 'd') && (letter <= 'f')) {
        return '3';
    }
    if ((letter >= 'g') && (letter <= 'i')) {
        return '4';
    }
    if ((letter >= 'j') && (letter <= 'l')) {
        return '5';
    }
    if ((letter >= 'm') && (letter <= 'o')) {
        return '6';
    }
    if ((letter >= 'p') && (letter <= 's')) {
        return '7';
    }
    if ((letter >= 't') && (letter <= 'v')) {
        return '8';
    }
    if ((letter >= 'w') && (letter <= 'z')) {
        return '9';
    }
    return 0;
}

/* 音节与数字串比较。mode = 0：要求完全相等；mode = 1：只要前缀一致。
 * 返回 1 = 命中。 */
static int syl_digits_match(const char *syl, const char *digits, unsigned ndigits, int prefix)
{
    unsigned i;

    for (i = 0U; i < ndigits; i++) {
        char d;

        if (syl[i] == '\0') {
            return 0;   /* 音节比输入短：既不可能完全相等，也不是"输入到一半" */
        }
        d = fml_t9_digit_of(syl[i]);
        if (d != digits[i]) {
            return 0;
        }
    }
    if (prefix != 0) {
        return 1;
    }
    return (syl[ndigits] == '\0') ? 1 : 0;
}

/*
 * 匹配音节列表。完全匹配优先；只有完全匹配为 0 时才退到前缀匹配。
 *
 * 完全匹配可能命中多个（xiong / zhong 同为 94664），**全部**收进列表 ——
 * 只留一个就是 P5c 上板那次 FAIL 的起因（94664 落到 xiong，"中"永远出不来）。
 * 用静态数组而不是让调用方传缓冲：只有 app_notepad 一个消费者，且它要的就是
 * "当前这串数字的匹配集"，语义上本来就是一份。
 */
static const char *s_match[FML_T9_MATCH_MAX];
static unsigned s_match_n;

static void match_build(const char *digits, unsigned ndigits)
{
    unsigned i;

    s_match_n = 0U;
    if ((digits == NULL) || (ndigits == 0U) || (ndigits > FML_T9_MAX_DIGITS)) {
        return;
    }
    for (i = 0U; (i < T9_SYL_COUNT) && (s_match_n < FML_T9_MATCH_MAX); i++) {
        if (syl_digits_match(fml_t9_syl[i], digits, ndigits, 0) != 0) {
            s_match[s_match_n++] = fml_t9_syl[i];
        }
    }
    if (s_match_n > 0U) {
        return;     /* 有完全匹配就不看前缀：否则 "zhong" 会跟一堆 "z..." 混在一起 */
    }
    for (i = 0U; (i < T9_SYL_COUNT) && (s_match_n < FML_T9_MATCH_MAX); i++) {
        if (syl_digits_match(fml_t9_syl[i], digits, ndigits, 1) != 0) {
            s_match[s_match_n++] = fml_t9_syl[i];
        }
    }
}

unsigned fml_t9_match_count(const char *digits, unsigned ndigits)
{
    match_build(digits, ndigits);
    return s_match_n;
}

const char *fml_t9_match_at(const char *digits, unsigned ndigits, unsigned nth)
{
    match_build(digits, ndigits);
    return (nth < s_match_n) ? s_match[nth] : NULL;
}

int fml_t9_resolve(const char *digits, unsigned ndigits, const char **syl_out)
{
    const char *s = fml_t9_match_at(digits, ndigits, 0U);

    if (syl_out != NULL) {
        *syl_out = s;
    }
    return (s != NULL) ? 1 : 0;
}

/* 音节 → 表内下标；-1 = 不在表里。线性扫 396 条，点击级调用量下不必二分。 */
static int syl_index(const char *syl)
{
    unsigned i;

    if (syl == NULL) {
        return -1;
    }
    for (i = 0U; i < T9_SYL_COUNT; i++) {
        const char *a = fml_t9_syl[i];
        unsigned k = 0U;

        while ((a[k] != '\0') && (syl[k] != '\0') && (a[k] == syl[k])) {
            k++;
        }
        if ((a[k] == '\0') && (syl[k] == '\0')) {
            return (int)i;
        }
    }
    return -1;
}

unsigned fml_t9_hans_total(const char *syl)
{
    int idx = syl_index(syl);

    return (idx < 0) ? 0U : (unsigned)fml_t9_syl_count[idx];
}

uint16_t fml_t9_hans_at(const char *syl, unsigned idx)
{
    int si = syl_index(syl);

    if (si < 0) {
        return 0U;
    }
    if (idx >= (unsigned)fml_t9_syl_count[si]) {
        return 0U;
    }
    return fml_t9_han[(unsigned)fml_t9_syl_first[si] + idx];
}

unsigned fml_t9_syl_total(void)
{
    return T9_SYL_COUNT;
}
