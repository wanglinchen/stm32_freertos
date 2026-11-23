/*
 * 最小扁平设备树（FDT/DTB）解析层。
 *
 * 设计口径对齐 stm32-lrtos 的 drivers/of：设备事实来自 DTB，而不是构建期由
 * Python 摊成 C 常量的生成头。这里只实现板级初始化真正需要的那一小片：
 * 按 compatible 找节点、数/取子节点、读 u32 与 string 属性。
 *
 * 不做（有意）：phandle 解引用、reg 地址翻译（ranges）、pinctrl、alias、
 * 属性迭代器、字符串表缓存。需要时按需加，不要一次搬完 lrtos 的 of 层 ——
 * 本工程 .text 余量只有几百字节。
 */
#ifndef BSL_FDT_H
#define BSL_FDT_H

#include <stdint.h>

/* 节点句柄：结构块内该节点名字的起点。不透明，只能配合下面的 API 使用。 */
typedef const void *bsl_fdt_node_t;

/* 校验内嵌 DTB 头部。0 = 可用；负值为具体失败原因（见 bsl_fdt.c 注释）。 */
int bsl_fdt_init(void);

/* 按 compatible 找第 index 个匹配节点（全树深度优先，index 从 0 起）。
 * 同一节点写了多个 compatible 串时只算一次。找不到返回 NULL。 */
bsl_fdt_node_t bsl_fdt_find(const char *compatible, unsigned index);

/* 节点的直接子节点数。 */
unsigned bsl_fdt_child_count(bsl_fdt_node_t node);

/* 第 index 个直接子节点；越界返回 NULL。 */
bsl_fdt_node_t bsl_fdt_child(bsl_fdt_node_t node, unsigned index);

/* 读单 cell 的 u32 属性。缺失或值不足 4 字节返回 0。 */
int bsl_fdt_prop_u32(bsl_fdt_node_t node, const char *name, uint32_t *out);

/* 读多 cell 属性（如 gpios = <&gpiob 1 1> 是 3 cell）。返回实际写出的
 * cell 数；缺失或长度不足返回 0。out 至少能放 want 个。 */
unsigned bsl_fdt_prop_cells(bsl_fdt_node_t node, const char *name, uint32_t *out, unsigned want);

/* flag 属性（如 active-low / pressed-high）。**按值判，不按"在不在"判**：
 *   属性不存在                     → 0
 *   布尔写法 `<name>;`（长度 0）    → 1
 *   `<name> = <v>`（4 字节）        → v != 0
 *   其它长度（多 cell 等）          → 0（形态不认识就不猜）
 * 只判"属性存在"会把 `active-low = <0>` 读成真，LED/按键极性整体反相。 */
int bsl_fdt_prop_flag(bsl_fdt_node_t node, const char *name);

/* 按 phandle 找节点（解 gpios = <&gpiob ...> 这类引用）。找不到返回 NULL。 */
bsl_fdt_node_t bsl_fdt_by_phandle(uint32_t phandle);

/* 读 string 属性（stringlist 时返回第一串）。缺失返回 NULL。 */
const char *bsl_fdt_prop_string(bsl_fdt_node_t node, const char *name);

#endif /* BSL_FDT_H */
