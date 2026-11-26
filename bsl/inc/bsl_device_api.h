#ifndef BSL_DEVICE_API_H
#define BSL_DEVICE_API_H

#include <stdint.h>

#include "board_beep.h"
#include "board_debug_uart.h"
#include "board_key.h"
#include "board_led.h"
#include "bsl_rtc.h"

/* 各 compatible 在运行期的 ops。
 *
 * 这些结构里**没有** init：初始化只有 `bsl_device_driver_t.init` 一个入口，
 * 由 bsl_init_run() 按 init_level 调用。早前每个 api 结构都带一个
 * `void (*init)(void)`，与 driver 的 init 指向同一个函数，但全仓没有任何代码
 * 通过 api 调用它 —— 于是"改 api 里的 .init 以为改了初始化"这类静默无效
 * 有了可乘之机（与 DTS 里 gpios flags / pressed-* 双写同型）。已删除。
 *
 * 原先的 `bsl_debug_uart_api_t` / `bsl_api_debug_uart` 也已删除：`puts` 全仓零
 * 消费者（`bsl_log` 直接调 `board_debug_uart_puts`），留着只是一套"看起来能
 * 走 api 打日志"的面子接口。
 *
 * 类型与符号的对应关系不写在代码里：binding 用 `api_type` / `api_sym` 声明，
 * 生成期产出 `bsl_devapi_<node>()` 访问器（board_device_registry.h）。 */

typedef struct {
    void (*set)(board_led_id_t id, int on);
    void (*on)(board_led_id_t id);
    void (*off)(board_led_id_t id);
    void (*toggle)(board_led_id_t id);
} bsl_gpio_leds_api_t;

typedef struct {
    int (*pressed)(board_key_id_t id);
    int (*pin_high)(board_key_id_t id);
} bsl_gpio_keys_api_t;

typedef struct {
    void (*set)(int on);
    void (*on)(void);
    void (*off)(void);
} bsl_beep_api_t;

/* SDRAM 只暴露"帧缓冲能放哪"这一件事：基址与字节数。
 * 不提供 read/write 包装 —— 消费方拿到基址就是普通指针运算，再包一层只会
 * 让每次取像素多一次函数调用。 */
typedef struct {
    void *(*base)(void);
    uint32_t (*size)(void);
} bsl_sdram_api_t;

/* 面板与 sdram 同口径：只说"帧缓冲在哪、多宽多高"。
 * 不在这里放 fill/线/字 —— 那些是像素原语，归 bsl_fb；api 一旦开始长操作，
 * 每个应用都会从自己的入口再抄一遍。 */
typedef struct {
    void *(*framebuffer)(void);
    uint32_t (*width)(void);
    uint32_t (*height)(void);
} bsl_panel_api_t;

/* 触摸：读一帧触点。返回点数，0xFFFFFFFF 表示总线错（见 board_touch.h 的
 * BOARD_TOUCH_BUS_ERROR）。product_id 只用于日志与 POST 行，不参与控制。 */
typedef struct {
    unsigned (*read)(int32_t *x, int32_t *y);
    const char *(*product_id)(void);
} bsl_touch_api_t;

/* SPI NOR：JEDEC、容量/页/扇区、按字节读、擦扇区、按页写。
 * 读/写失败必须返回非 BSL_OK —— FatFs 侧靠这个返回值把 RES_ERROR 传回上层，
 * 静默返回成功会让文件系统以为写进去了。 */
typedef struct {
    uint32_t (*jedec)(void);
    uint32_t (*size)(void);
    uint32_t (*page_size)(void);
    uint32_t (*sector_size)(void);
    bsl_err_t (*read)(uint32_t addr, void *buf, uint32_t len);
    bsl_err_t (*erase_sector)(uint32_t addr);
    bsl_err_t (*write)(uint32_t addr, const void *buf, uint32_t len);
} bsl_nor_api_t;

/* RTC：只暴露"走时源是谁、好了没、现在几点"。
 * 不在这里放 set/唤醒/闹钟 —— 那是应用的需求，不是设备契约；现在没有任何
 * 消费者，放上去就是又一套零调用的面子接口（与上面删掉的两个同型）。 */
typedef struct {
    bsl_rtc_src_t (*clock_source)(void);
    int (*ready)(void);
    bsl_err_t (*get)(bsl_rtc_time_t *out);
} bsl_rtc_api_t;

/* SAI1（音频播放）：采样率、帧缓冲、播放与停止，以及上一次播放的实测结果。
 *
 * buffer 暴露成函数而不是让 fml_audio 自己申请：音频是独占资源，8 KB 只花一次；
 * 两份缓冲等于两份相位不同的数据流，谁也不知道哪份在响。
 * last_error 单独给：`play` 返回 OK 但 OVRUDR 亮着也是坏消息（欠载 = 声音断续）。 */
typedef struct {
    uint32_t (*sample_rate)(void);
    uint32_t *(*buffer)(void);
    bsl_err_t (*start)(uint32_t n_frames);
    bsl_err_t (*play_blocking)(uint32_t n_frames, uint32_t *elapsed_us);
    bsl_err_t (*stop)(void);
    uint32_t (*last_us)(void);
    uint32_t (*last_error)(void);
} bsl_sai_api_t;

/* WM8978：只写器件，所以"起来了"只能靠 ACK 计数证明（ack_count == reg_count）。
 * set_volume 是初始化之后的独立动作，不进 POST 的事务计数。 */
typedef struct {
    int (*ready)(void);
    unsigned (*ack_count)(void);
    unsigned (*reg_count)(void);
    bsl_err_t (*set_volume)(uint8_t vol);
} bsl_codec_api_t;

/* MPU9250（P6d）：身份 + 原始读数。
 * who / mag_wia 单独给而不是合成一个"健康度"布尔：型号不对与磁力计没通是
 * 两件不同的坏消息，合成一个就再也分不开了（而 P6c-0 正是靠 who 值定的型号）。 */
typedef struct {
    int (*ready)(void);
    uint8_t (*who)(void);
    uint8_t (*mag_wia)(void);
    bsl_err_t (*read)(int16_t acc[3], int16_t gyr[3]);
} bsl_imu_api_t;

/* AP3216C（P6d）：可读回的系统寄存器已经由 init 校验，这里只给数据。 */
typedef struct {
    int (*ready)(void);
    bsl_err_t (*read)(uint16_t *ir, uint16_t *ps, uint16_t *als);
} bsl_als_api_t;

/* 24C02（P6d）：读/写是**设备语义**（含 tWR 等待），不是总线语义，
 * 所以 write 在本层暴露；应用侧只应使用 read（设置持久化将来再开写）。 */
typedef struct {
    int (*ready)(void);
    bsl_err_t (*read)(uint8_t addr8, uint8_t *buf, unsigned len);
    bsl_err_t (*write)(uint8_t addr8, uint8_t value);
} bsl_eeprom_api_t;

/* TPAD（P6d）：基线/门限上报 + 扫描。
 * scan 的返回值口径是 int 而不是 bool：未初始化要能被区分出来（BSL_ERR_NOINIT），
 * 否则"没初始化"和"没按下"在应用里长得一样。 */
typedef struct {
    int (*ready)(void);
    uint16_t (*baseline)(void);
    uint16_t (*threshold)(void);
    int (*scan)(int continuous);
} bsl_tpad_api_t;


/* CAN1（P6e-1）：位率 + 分频（timing 一次给 brp/tseg，POST 三个数一起打）
 * + 发送 + 非阻塞收帧。send 只证"邮箱受理"，"对方收到"要靠 recv ——
 * 回环模式下这两步合起来才是完整判据。 */
typedef struct {
    int (*ready)(void);
    uint32_t (*bit_rate)(void);
    void (*timing)(uint32_t *brp, uint32_t *tseg);
    bsl_err_t (*send)(uint32_t id, const uint8_t *data, uint8_t len);
    int (*recv)(uint32_t *id, uint8_t *data, uint8_t *len);
} bsl_can_api_t;

/* USART3（P6e-1）：波特率/BRR + 发一字节（带实测耗时）+ 空閒判帧取帧
 * + inject（POST 注入自证，绕过硬件）。
 * send 的 *us 是判据来源：8N1 = 10 bit/帧，连发 n 字节的总时长应等于
 * n*10*bit_time（单字节口径含 up to 1 bit 的启停量化，不用来判分频）。 */
typedef struct {
    int (*ready)(void);
    uint32_t (*baud)(void);
    uint32_t (*brr)(void);
    bsl_err_t (*send_byte)(uint8_t b, uint32_t *us);
    bsl_err_t (*send)(const uint8_t *buf, unsigned n, uint32_t *us);
    int (*recv_frame)(uint8_t *buf, unsigned max, unsigned *len);
    void (*inject)(uint8_t b);
} bsl_uart_api_t;

/* NAND（P9a 只读 + P9a-2 写通道）：info 一次给全 —— ID 5 字节 + 页/块几何 + 实际
 * 走通的 bank 号（参考工程写 Bank3/PG9，而 F429 的 PG9 多数封装是 NCE2，
 * 驱动两个 bank 都配、谁能读出 ID 用谁，这个数就是现场事实）。
 *
 * P9a-2 加的三个方法只到"原料"这一层：erase_block / write_page 各自返回
 * BSL_OK 只代表命令序列跑完且状态寄存器 I/O0=0，**不代表数据可信**
 * （ECC 没开、坏块没管）；调用方必须自己读回比对，POST 就是这么用的。
 * is_bad_block 返回 1 = 坏块或判不出来 —— 判不准时一律当坏块，是唯一安全的方向。 */
typedef struct {
    int (*ready)(void);
    void (*info)(uint8_t id[5], uint16_t *page_main, uint16_t *page_spare,
                 uint32_t *block_pages, uint32_t *blocks, unsigned *bank);
    bsl_err_t (*read_page)(uint32_t page, uint16_t col, uint8_t *buf,
                           uint16_t len);
    bsl_err_t (*erase_block)(uint32_t block);
    bsl_err_t (*write_page)(uint32_t page, uint16_t col, const uint8_t *buf,
                            uint16_t len);
    int (*is_bad_block)(uint32_t block);
} bsl_nand_api_t;

/* DMA（P9c-1）：DMA2 内存→内存。done_words 是"实际搬了多少字"的读数，
 * 判据用它，而不是只看函数返回值。 */
typedef struct {
    int (*ready)(void);
    bsl_err_t (*memcopy)(const void *src, void *dst, unsigned words,
                         unsigned *done_words);
} bsl_dma_api_t;

/* PWM（P9c-2）：TIM3_CH2 背光。
 *
 * clock_hz 是定时器**计数时钟**（APB1×2 = 84 MHz），frequency 是 DTS 声明的目标，
 * 这两个必须一起看：只看 frequency 无法判断"是按 42 MHz 还是 84 MHz 算的 PSC"。
 * measure_freq 数的是 CCxIF 硬件标志，不是把 PSC/ARR 抄回来重算。 */
typedef struct {
    int (*ready)(void);
    uint32_t (*clock_hz)(void);
    uint32_t (*frequency)(void);
    bsl_err_t (*set_duty)(uint32_t permille);
    uint32_t (*duty)(void);
    bsl_err_t (*measure_freq)(uint32_t *hz);
    void (*timing)(uint32_t *psc, uint32_t *arr, uint32_t *ccr);
} bsl_pwm_api_t;

/* EXTI（P9c-2）：EXTI0 ← PA0。
 *
 * count 是"边沿总数"的唯一读数；pulse_pin / trigger_sw 两个激励函数都返回
 * 本阶段结束时的计数，"打进去几个边沿、中断进来几次"必须在同一个数上对齐，
 * 否则"硬件来了、ISR 没进"与"根本没发生边沿"分不开。
 * info 把 line/src/edge 三项回给上层打日志，省得 POST 里再造一遍常量。 */
typedef struct {
    int (*ready)(void);
    unsigned (*count)(void);
    void (*clear)(void);
    unsigned (*pending)(void);
    void (*info)(unsigned *line, unsigned *src, int *rising);
    unsigned (*pulse_pin)(unsigned n);
    unsigned (*trigger_sw)(unsigned n);
} bsl_exti_api_t;

#endif