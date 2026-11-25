#ifndef FML_QR_H
#define FML_QR_H

/*
 * 二维码编码（P6e-2）。
 *
 * 算法不是本工程写的：源是参考工程（正点原子 实验63）Middlewares/QR_ENCODE
 * （V1.1，其自身移植自网友"旮旯旭"的二维码生成工程）。fml/src/fml_qr.c 由
 * build/gen_qr.py 从三份源码机械生成，算法与参数（纠错等级 L、掩码 0、
 * 版本按内容自动选）与参考工程逐条一致，改算法请改源仓重跑脚本。
 *
 * 对外只有四个入口。参考工程把整个 _qr_encode 结构（40 KB）暴露给应用读
 * `m_byModuleData[i][j]`；本工程把结构留在编译单元里 —— 应用拿不到中间态，
 * 编码失败时也不会读到上一轮的残矩阵。矩阵按 (x, y) 逐格取，调用方
 * （app_qr 画一个格一个方块）本来就是逐格画，没有整块拷贝的必要。
 *
 * 内存：结构在 bss（约 40 KB），不进堆 —— 参考工程用 gui_memin_malloc，
 * 本工程没有能容纳它的通用堆。flash：表约 25 KB、代码约 40 KB。
 *
 * 编码长度上限由算法自身决定（版本 40 + 等级 L 约 2953 字节）；app_qr 只喂
 * 预设短串，远未到边界。空串与 NULL 一律返回负值。
 */

#define FML_QR_MAX_MODULES 177U   /* 版本 40：21 + (40-1)*4 = 177 */

/* 编码一段 ASCII/NUL 结尾文本。0 成功；-1 入参空；-2 编码失败（内容过长/空串）。 */
int fml_qr_encode(const char *text);

/* 最近一次编码结果的边长（模块数，= 21 + (ver-1)*4）。未编码过为 0。 */
unsigned fml_qr_size(void);

/* 最近一次编码结果的版本号（1..40）。 */
unsigned fml_qr_version(void);

/* 最近一次编码结果在 (x, y) 处的模块：1 黑、0 白；越界返回 0。 */
int fml_qr_module(unsigned x, unsigned y);

/* 最近一次编码结果的黑模块总数（app_qr 与 POST 判据都用它，避免各算一遍）。 */
unsigned fml_qr_dark(void);

#endif /* FML_QR_H */
