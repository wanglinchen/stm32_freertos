#ifndef FML_BUTTON_H
#define FML_BUTTON_H

/*
 * 底板按键 → 蜂鸣器反馈任务（依赖 bsl 的 board_key / board_beep）。
 *
 * KEY1 = PA0/WKUP（外部 4.7K 下拉，按下读到高）
 * KEY2 = PC13/TAMPER（外部 4.7K 下拉，按下读到高）
 * 按住任一只键时蜂鸣器响（PI11 → R25 1K → SS8050 基极），松开即停。
 *
 * 存在的意义不是"有个 demo"，而是让 bsl 的按键/蜂鸣器实现真正进镜像：
 * 此前 board_key_init/pressed、board_beep_init/on/off 全仓零引用，
 * 被 -Wl,--gc-sections 整段丢弃，板上有按键却按不动，也没有任何报错。
 */

void fml_button_start(void);

#endif
