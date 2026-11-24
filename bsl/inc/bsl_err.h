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
} bsl_err_t;

#endif