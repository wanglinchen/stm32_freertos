/*
 * 板级事实的运行期访问器：数据全部来自内嵌 DTB（bsl_fdt 解析），
 * 不再有任何构建期生成的 C 常量头。
 *
 * 为什么要单独一层：固件里 bsl/board_*、osl 的 panic 需要「端口 / 引脚 / 极性 /
 * 地址 / 波特率」这些板级事实，而它们现在只存在于 DTS。把取值集中到这里，
 * 各驱动只依赖函数，不依赖具体节点名，也不各自复制一份解析逻辑。
 *
 * 例外：panic 播报路径不能用本层（栈可能已崩、不能再遍历 FDT），
 * 它仍读 osl/inc/board_panic_hw.h 的手写常量。
 */
#ifndef BOARD_RT_H
#define BOARD_RT_H

#include <stdint.h>

#include "stm32f4xx_hal.h"

/* HSE 频率（Hz）；DTB 里没有 clocks 节点时返回 0。 */
uint32_t board_rt_hse_hz(void);

/* SDRAM（FMC Bank5）：基址 / 字节数 / SDCR / SDTR / 刷新计数。
 * 返回 1 表示五项都拿到了；任一缺失返回 0，此时 *out 不被写。
 * 引脚按端口聚合（一个端口一项，掩码 + AF），不是逐脚。 */
int board_rt_sdram(uintptr_t *base, uint32_t *size, uint32_t *sdcr,
                   uint32_t *sdtr, uint32_t *refresh_count);
unsigned board_rt_sdram_pin_groups(void);
GPIO_TypeDef *board_rt_sdram_pin_port(unsigned index);
uint16_t board_rt_sdram_pin_mask(unsigned index);
uint32_t board_rt_sdram_pin_af(unsigned index);

/* RGB 面板（LTDC）：时序、像素时钟的 PLLSAI 参数、像素格式、帧缓冲基址、背光脚。
 * 用结构体而不是十三个标量 getter：这些值必须成套出现 —— 少一项就配不出时序，
 * 而单个 0 值（比如 hbp=0）看起来像合法参数，现场表现成"图像整体偏移"，
 * 没有任何读数指向 DTB。任一属性缺失时 board_rt_panel() 返回 0、*out 不被写。 */
typedef struct {
    uint32_t width;          /* 有效宽度（像素） */
    uint32_t height;         /* 有效高度（像素） */
    uint32_t hsw;            /* 水平同步宽度 */
    uint32_t hbp;            /* 水平后廊 */
    uint32_t hfp;            /* 水平前廊 */
    uint32_t vsw;            /* 垂直同步宽度 */
    uint32_t vbp;            /* 垂直后廊 */
    uint32_t vfp;            /* 垂直前廊 */
    uint32_t pixel_format;   /* LTDC PFCR 编码：0=ARGB8888 1=RGB888 2=RGB565 3=ARGB1555 ... */
    uint32_t pllsai_n;       /* PLLSAI 倍频：VCO = PLLIN × N */
    uint32_t pllsai_r;       /* PLLSAI R 分频，输出即像素时钟源 */
    uint32_t pllsai_divr;    /* DCKCFGR[17:16]：0=/2 1=/4 2=/8 3=/16 */
    uintptr_t framebuffer;   /* 帧缓冲基址（framebuffer phandle 所指节点的 reg[0]） */
} board_rt_panel_t;

int board_rt_panel(board_rt_panel_t *out);

/* LTDC 引脚按端口聚合（一个端口一项，掩码 + AF），含 DE/VS/HS/CLK 与数据脚。 */
unsigned board_rt_panel_pin_groups(void);
GPIO_TypeDef *board_rt_panel_pin_port(unsigned index);
uint16_t board_rt_panel_pin_mask(unsigned index);
uint32_t board_rt_panel_pin_af(unsigned index);

/* 背光脚：active_high 表示"高电平点亮"。返回 1 表示三项都拿到。 */
int board_rt_panel_backlight(GPIO_TypeDef **port, unsigned *pin, int *active_high);

/* 电容触摸（GT9xxx）：I2C 7 位地址、复位/中断脚、这条 I2C 的两脚、坐标范围。
 * 与 panel 一样成套返回：缺一项就既读不到 PID、也清不了状态寄存器，而失败现场
 * 只表现为"触点永远是 0"，看起来像"屏没问题、只是没按"。
 *
 * 注意：触摸走的是**软件 I2C**（PH6/PI3），与主 I2C（pcf8574/蜂鸣器那条）不是
 * 同一条总线 —— 两条总线各自一套引脚，都在这里给。 */
typedef struct {
    uint8_t addr7;
    unsigned max_points;
    GPIO_TypeDef *rst_port;
    unsigned rst_pin;
    int rst_active_low;
    GPIO_TypeDef *irq_port;  /* DTB 没写时为 NULL */
    unsigned irq_pin;
    GPIO_TypeDef *scl_port;
    unsigned scl_pin;
    GPIO_TypeDef *sda_port;
    unsigned sda_pin;
} board_rt_touch_t;

int board_rt_touch(board_rt_touch_t *out);

/* SPI NOR（W25Q256，32 MB）：SPI 实例号、三根信号线（各自 port/pin + AF）、
 * 片选脚及其极性、容量/页/扇区。
 *
 * 三根线分开给而不是一个 mask：驱动要按 SCK/MISO/MOSI 各配一次 AF，一个 mask
 * 无法表达"哪一位是 MISO"，只能靠代码里硬编码位索引。 */
typedef struct {
    SPI_TypeDef *inst;
    GPIO_TypeDef *sck_port;
    unsigned sck_pin;
    GPIO_TypeDef *miso_port;
    unsigned miso_pin;
    GPIO_TypeDef *mosi_port;
    unsigned mosi_pin;
    unsigned af;
    GPIO_TypeDef *cs_port;
    unsigned cs_pin;
    int cs_active_low; /* 1 = 低电平选中 */
    uint32_t size;     /* 字节 */
    uint32_t page_size;
    uint32_t sector_size;
} board_rt_nor_t;

int board_rt_nor(board_rt_nor_t *out);

/* RTC：走时源偏好与 LSE 晶振频率。两项都是板级事实（晶振焊没焊、焊的是多少 Hz）；
 * 寄存器基址与日历编码是芯片事实，不在这里。
 * 返回 1 = 节点在且 clock-source 是认识的形态（"lse" / "lsi"）。 */
typedef struct {
    int use_lsi;     /* 1 = DTS 明确要求走片内 LSI */
    uint32_t lse_hz; /* LSE 频率（Hz）；0 = DTS 未声明 */
} board_rt_rtc_t;

int board_rt_rtc(board_rt_rtc_t *out);

/* ADC 内部温度通道：只有 VREF+ 电压是板级事实。它是 TS_CAL 有效性的门限 ——
 * 校准值只在 VDDA=VREF+=3.3 V±0.1 V 下成立，电压偏离时温度会整体偏移。
 * 通道号 16 与采样时间是手册事实，写在 bsl_adc 里。 */
typedef struct {
    uint32_t vref_mv;
} board_rt_adc_t;

int board_rt_adc_temp(board_rt_adc_t *out);

/* SAI1（音频播放走 block A）：采样率、MCLK 分频、PLLI2S 的三个数、codec 的 7 位地址。
 *
 * 成套返回的理由与 panel 相同：少一项就配不出位时钟，而现场只表现为"没有声音"，
 * 没有任何读数指向 DTB。codec_addr7 = 0 表示 DTB 没挂 codec 节点（地址 0 不是
 * 合法的 7 位地址），消费方据此直接失败，不去猜 0x1A。
 *
 * sample-rate 是 DTS 声明的目标值；实际 FS 由 bsl_sai 按 PLL 寄存器算出 ——
 * 两个数是"想要的"与"配出来的"，POST 行两个都报。 */
typedef struct {
    uint32_t sample_rate;
    uint32_t mck_div;      /* CR1.MCKDIV；MCLK = SAI_CK/(mck_div*2)（HAL 口径） */
    uint32_t pll2_n;
    uint32_t pll2_q;
    uint32_t pll2_divq;    /* DCKCFGR.PLLI2SDIVQ：0 表示 ÷1 */
    uint8_t codec_addr7;
} board_rt_sai_t;

int board_rt_sai(board_rt_sai_t *out);

/* SAI 引脚按端口聚合（一个端口一项，掩码 + AF）。 */
unsigned board_rt_sai_pin_groups(void);
GPIO_TypeDef *board_rt_sai_pin_port(unsigned index);
uint16_t board_rt_sai_pin_mask(unsigned index);
uint32_t board_rt_sai_pin_af(unsigned index);

unsigned board_rt_led_count(void);
GPIO_TypeDef *board_rt_led_port(void);
uint16_t board_rt_led_pin(unsigned index);
int board_rt_led_active_low(void);

unsigned board_rt_key_count(void);
GPIO_TypeDef *board_rt_key_port(unsigned index);
uint16_t board_rt_key_pin(unsigned index);
GPIO_PinState board_rt_key_pressed(unsigned index);

GPIO_TypeDef *board_rt_i2c_scl_port(void);
uint16_t board_rt_i2c_scl_pin(void);
GPIO_TypeDef *board_rt_i2c_sda_port(void);
uint16_t board_rt_i2c_sda_pin(void);
int board_rt_i2c_ready(void);

int board_rt_beep_via_pcf8574(void);
uint8_t board_rt_pcf8574_addr7(void);
unsigned board_rt_beep_bit(void);
int board_rt_beep_active_high(void);

/* 调试串口：reg 基址 / 波特率 / TX·RX 端口字母与脚号。任一项缺失返回 0。 */
int board_rt_uart(uintptr_t *reg, uint32_t *baud, char *tx_port,
                  unsigned *tx_pin, char *rx_port, unsigned *rx_pin);

/* phandle（gpios 首 cell）→ GPIO 端口；找不到返回 NULL。 */
GPIO_TypeDef *board_rt_port_of_phandle(uint32_t phandle);

#endif /* BOARD_RT_H */
