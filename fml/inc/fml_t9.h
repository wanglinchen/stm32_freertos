#ifndef FML_T9_H
#define FML_T9_H

#include <stdint.h>

/*
 * 九宫格拼音输入法（T9）：数字键 → 拼音音节 → 汉字候选。
 *
 * 只做"引擎"，不做界面：按键布局、候选分页、文本编辑都在 `app_notepad`。
 * 这条分界是按 §3 定的 —— fml 层只回答"这串数字对应哪个音节、这个音节第几个
 * 汉字是什么码"，界面怎么摆是应用的事。
 *
 * 词库：GB2312 一级汉字 3755 个、396 个音节，组内按 GBK 升序（`fml/tools/
 * gen_pinyin.py` 生成，`fml_t9_table.c`）。只取默认读音：多音字展开会让表
 * 翻倍，而记事本用不到"重"和"行"的每个读音都排进候选。
 */

/* 最长拼音音节 6 个字母（"zhuang"/"chuang"）。按键序列给到 8 位，多余的
 * 位数在 resolve 里自然匹配不到（不会越界读表）。 */
#define FML_T9_MAX_DIGITS 8U

/* 字母 → 数字键（2..9）。非 a-z 返回 0：调用方据此判"这不是拼音字符"。 */
char fml_t9_digit_of(char letter);

/*
 * 数字串能对应的音节可能有**多个**：`xiong` 与 `zhong` 都是 94664（x/z 都在 9 键，
 * i/h 都在 4 键，o 在 6 键，n 在 6 键，g 在 4 键）。P5c 上板实测就栽在这里：
 * 只要"取表里第一个"，94664 会落到 xiong（首字 0xD0D6 "胸"），而用户想打的
 * "中"在 zhong 组里 —— 功能没坏，是"只给一个音节"这个接口错了。
 *
 * 所以匹配结果按"列表"暴露，由界面决定怎么把多个音节的候选摊开：
 *   match_count —— 有几个音节匹配（完全匹配优先；完全匹配为 0 才退到前缀匹配）
 *   match_at    —— 第 nth 个匹配音节的字符串（表内常量，不复制）
 * 顺序 = 音节表的字母序，稳定可复现（列表顺序变了，POST 的按位点击会当场失败）。
 */
#define FML_T9_MATCH_MAX 8U

unsigned fml_t9_match_count(const char *digits, unsigned ndigits);
const char *fml_t9_match_at(const char *digits, unsigned ndigits, unsigned nth);

/* 数字串 → **首个**匹配的音节（等价于 `match_at(digits, ndigits, 0)`）。
 * 单独留下是因为"只想问有没有、第一个是谁"的调用方不必自己管列表；
 * 但**不要**用它决定候选，多音节歧义会漏字（见上）。返回 1 = 找到。 */
int fml_t9_resolve(const char *digits, unsigned ndigits, const char **syl_out);

/* 某音节下的汉字总数；音节不在表里返回 0。 */
unsigned fml_t9_hans_total(const char *syl);

/* 某音节下第 idx 个汉字的 GBK 码（0xD6D0 = "中"）；越界或音节不存在返回 0。 */
uint16_t fml_t9_hans_at(const char *syl, unsigned idx);

/* 音节表条目数（诊断/POST 用；不参与任何判据）。 */
unsigned fml_t9_syl_total(void);

#endif /* FML_T9_H */
