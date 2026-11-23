#ifndef BSL_ERR_H
#define BSL_ERR_H

typedef enum {
    BSL_OK = 0,
    BSL_ERR_IO = -1,
    BSL_ERR_TIMEOUT = -2,
    BSL_ERR_INVAL = -3,
} bsl_err_t;

#endif