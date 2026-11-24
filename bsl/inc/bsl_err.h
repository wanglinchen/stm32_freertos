#ifndef BSL_ERR_H
#define BSL_ERR_H

/*
 * 统一错误码。`BSL_ERR_NOSUPPORT` 是 P5c 为 `fml_picture` 加的：解图时要分清
 * "文件打不开/读失败"（IO，可能是路径错、也可能是卷坏了）与"打开了但不是能吃
 * 的格式"（不支持）。混成一个码，POST 只能看到一句失败，现场得靠猜。
 */
typedef enum {
    BSL_OK = 0,
    BSL_ERR_IO = -1,
    BSL_ERR_TIMEOUT = -2,
    BSL_ERR_INVAL = -3,
    BSL_ERR_NOSUPPORT = -4,
    /* 没初始化就用了。P6a 加的：RTC 的"源起不来"与"没跑过 init"是两件事 ——
     * 都归 TIMEOUT 会让现场无法区分"晶振坏"与"驱动没被跑到"。 */
    BSL_ERR_NOINIT = -5,
} bsl_err_t;

#endif