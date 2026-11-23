#include "fml_panic.h"

#include "bsl_log.h"
#include "osl_panic.h"

/*
 * 故障播报的 fml 入口。
 *
 * 码表只有一份，在 osl/inc/osl_panic.h，osl 与 fml 都调用 osl_panic_report()。
 * 本层不再自己实现闪灯：以前这里用“同步闪 N 次 + 常亮”，而 osl 用“逐位二进制 +
 * 全亮”，两套码表的 1 和 2 直接撞码（osl 1 = 栈溢出，fml 1 = 调度器起不来），
 * 现场看到闪 1 次分不出来源。现在播报方式唯一，来源靠 g_panic_record 区分。
 *
 * 本函数永不返回：播报完关中断自旋。关中断是必需的 —— 它可能从任务上下文调用
 * （例如 blink_start / button_start 里 xTaskCreate 失败），调度器还活着时 blink
 * 任务下一拍就会把常亮翻回灭，故障证据当场消失。
 */
void fml_panic(unsigned code)
{
    /* 先把环形里保留的日志倒出来：report 之后是关中断自旋（若要等 IWDG 复位
     * 才恢复），没有第二次机会。这一步必须在最前 —— 导致故障的那几行比故障码
     * 本身更能定位问题。 */
    bsl_log_dump_ring();
    osl_panic_report((osl_panic_code_t)code);
}
