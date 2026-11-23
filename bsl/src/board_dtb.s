/*
 * 把 dtc 编出的整板 DTB 以 .incbin 方式链进固件，落在只读段 .dtb。
 *
 * 形态对齐 stm32-lrtos：设备的唯一描述是标准 devicetree 语法 → dtc → .dtb，
 * 运行期由 bsl_fdt 遍历解析，而不是由 Python 在构建期摊成 C 常量的“生成头”。
 *
 * 为什么用 .incbin 而不是把 DTB 单独烧到 flash 空白区（lrtos 的 0x08020000）：
 * 本工程只有 SWD 一条下发通道（无串口），单镜像能保证「固件与 DTB 一定成对」，
 * 省掉“刷了固件忘刷 DTB”后读到 0xFF 的整机故障。语义仍是运行期解析。
 *
 * .incbin 的路径相对 assembler 的工作目录：make 在 bsl/src 下编译本文件，
 * 因此写「上两级到工程根」的相对路径，再进 out/dts/（DTB 是构建产物，不入库）。
 * as 的 -I 只作用于 .include，不作用于 .incbin，所以这里不能用 -I 方案；
 * 目录层级若变动必须同步本行，`update` 缺产物时会在下面那句直接报错。
 */
    .syntax unified
    .section .dtb, "a", %progbits
    .global bsl_dtb_start
    .global bsl_dtb_end
    .align 4                /* ARM as 的 .align N 是 2^N 字节：这里是 16 B，不是 4 B。
                             * 段跨度会比 dtb 文件多出 ≤15 B 填充，对拍文件大小要用
                             * bsl_dtb_start/bsl_dtb_end（内容边界），不是 _sdtb/_edtb。 */
bsl_dtb_start:
    .incbin "../../out/dts/apollo.dtb"
bsl_dtb_end:
    .align 4                /* 同上：段尾推到 16 B 对齐，填充量随 dtb 大小变化 */
