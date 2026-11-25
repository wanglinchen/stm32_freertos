#ifndef BSL_DMA_H
#define BSL_DMA_H

#include "bsl_device.h"
#include "bsl_device_api.h"

/* DMA（P9c-1）：只做 DMA2 的内存→内存通道。
 *
 * 为什么是 DMA2：F4 的 DMA1 不支持内存→内存（参考工程自己的注释里也写了这条），
 * 拿 DMA1 做 M2M 会"配置成功、传输永不完成"，比不配更难查。
 *
 * 本批不把 DMA 接到任何外设（SAI 的音频流自己走 HAL 的单通道配置）：这里只证
 * "控制器、流、中断标志、数据搬运"这条链是对的，外设通道留给真正需要带宽的模块
 * （SDIO / DCMI / ADC 多通道）。 */
bsl_err_t bsl_dma_init(void);

extern const bsl_device_driver_t bsl_driver_brd_dma;
extern const bsl_dma_api_t bsl_api_brd_dma;

#endif /* BSL_DMA_H */
