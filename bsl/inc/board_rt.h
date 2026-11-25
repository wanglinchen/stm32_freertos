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

/* 面板：一份结构体，两种总线形态。
 *
 * 用结构体而不是十几个标量 getter：这些值必须成套出现 —— 少一项就配不出画面，
 * 而单个 0 值（比如 hbp=0）看起来像合法参数，现场表现成"图像整体偏移/白屏"，
 * 没有任何读数指向 DTB。任一该形态必需的属性缺失时 board_rt_panel() 返回 0、
 * *out 不被写。
 *
 * P7 之前这里只描述 LTDC 直驱的 RGB 屏（时序 + PLLSAI + 像素格式）。板上实插的是
 * NT35510 的 4.3" MCU 屏（480×800、FMC 8080 16 位并口），没有 hsw/hbp/pllsai 这些
 * 概念 —— 硬套那组字段的后果就是 LTDC 白扫一遍、屏一个像素也收不到。所以新增
 * bus 区分"像素怎么送"，两种形态共用的只有 width/height/pixel-format/帧缓冲/背光。 */
#define BOARD_RT_PANEL_BUS_RGB 0U      /* LTDC 直驱 RGB 屏（同步信号 + 像素时钟） */
#define BOARD_RT_PANEL_BUS_FMC8080 1U  /* FMC 8080 并口 MCU 屏（命令/数据 + 片选） */

typedef struct {
    uint32_t bus;            /* BOARD_RT_PANEL_BUS_* */
    uint32_t panel_id;       /* MCU 屏的 DTB panel-id（如 0x5510）；RGB 屏恒为 0 */
    uint32_t width;          /* 有效宽度（像素） */
    uint32_t height;         /* 有效高度（像素） */
    uint32_t hsw;            /* 水平同步宽度（仅 RGB） */
    uint32_t hbp;            /* 水平后廊（仅 RGB） */
    uint32_t hfp;            /* 水平前廊（仅 RGB） */
    uint32_t vsw;            /* 垂直同步宽度（仅 RGB） */
    uint32_t vbp;            /* 垂直后廊（仅 RGB） */
    uint32_t vfp;            /* 垂直前廊（仅 RGB） */
    uint32_t pixel_format;   /* 像素编码：RGB=LTDC PFCR，8080=0b101 即 RGB565 的常用编码 */
    uint32_t pllsai_n;       /* PLLSAI 倍频：VCO = PLLIN × N（仅 RGB） */
    uint32_t pllsai_r;       /* PLLSAI R 分频，输出即像素时钟源（仅 RGB） */
    uint32_t pllsai_divr;    /* DCKCFGR[17:16]：0=/2 1=/4 2=/8 3=/16（仅 RGB） */
    uintptr_t framebuffer;   /* 帧缓冲基址（framebuffer phandle 所指节点的 reg[0]） */
} board_rt_panel_t;

int board_rt_panel(board_rt_panel_t *out);

/* 面板引脚按端口聚合（一个端口一项，掩码 + AF）。
 * RGB 屏：DE/VS/HS/CLK + 数据脚，走 LTDC；
 * MCU 屏：只剩数据脚 D0..D15，走 FMC —— 控制脚见 board_rt_panel_ctrl()。 */
unsigned board_rt_panel_pin_groups(void);
GPIO_TypeDef *board_rt_panel_pin_port(unsigned index);
uint16_t board_rt_panel_pin_mask(unsigned index);
uint32_t board_rt_panel_pin_af(unsigned index);

/* MCU 屏的四根控制脚（各占一位，端口由 phandle 决定）。
 * 下标用 BOARD_RT_LCD_PIN_*；RGB 屏或 DTB 没写时返回 0。 */
#define BOARD_RT_LCD_PIN_CS 0U   /* 片选，FMC_NE1 = PD7 */
#define BOARD_RT_LCD_PIN_RS 1U   /* 命令/数据选择，FMC_A18 = PD13 */
#define BOARD_RT_LCD_PIN_NWE 2U  /* 写选通，FMC_NWE = PD5 */
#define BOARD_RT_LCD_PIN_NOE 3U  /* 读选通，FMC_NOE = PD4 */
#define BOARD_RT_LCD_PIN_COUNT 4U
int board_rt_panel_ctrl(unsigned index, GPIO_TypeDef **port, unsigned *pin, uint32_t *af);

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

/* MPU9250（六轴 + 片内 AK8963 磁力计）：两颗芯片各一个 7 位地址。
 * 磁力计的地址不是"再挂一颗 I2C 器件"，而是**MPU9250 内部的旁路**：只有先写
 * INT_PIN_CFG(0x37) 的 BYPASS_EN 位，AK8963 才出现在总线上。mag_addr7 由 DTB 给，
 * 消费方不得自己猜 0x0C —— P6c-0 那次 `i2c 0c r00=nack` 正是没开旁路时的正常现象。 */
typedef struct {
    uint8_t addr7;
    uint8_t mag_addr7; /* 0 = DTB 未声明，驱动按"不做磁力计自检"降级 */
} board_rt_mpu9250_t;

int board_rt_mpu9250(board_rt_mpu9250_t *out);

/* AP3216C（ALS / PS+IR 三合一）：只有 7 位地址是板级事实，寄存器表是器件事实。 */
typedef struct {
    uint8_t addr7;
} board_rt_ap3216c_t;

int board_rt_ap3216c(board_rt_ap3216c_t *out);

/* 24C02（256 B，8 位字地址）。 */
typedef struct {
    uint8_t addr7;
} board_rt_eeprom_t;

int board_rt_eeprom(board_rt_eeprom_t *out);

/* TPAD（电容触摸按键）：PA5 走 TIM2_CH1 输入捕获，充电时间就是"键值"。
 * 引脚 + 定时器 + 通道 + 计数参数成套返回 —— 少一项就量不出充电时间，
 * 而现场只表现为"永远按不到"，没有任何读数指向 DTB。
 * prescaler 决定分辨率（越小越灵敏）。
 *
 * **这里没有 gate-val**：2026-11-25 实测基线只有 17 个计数（PSC=20 → 4.5 MHz，
 * 即充电约 3.8 µs），此时"基线 + 100"等于要把读数放大到 7 倍才认按下 ——
 * 手指根本按不到。参考工程 `tpad.h` 里的 `TPAD_GATE_VAL 100` 也是个**零使用**的
 * 常量（`tpad_scan` 实际用的是倍率判据）。所以判定改成倍率口径（> 4/3 基线），
 * 不在这里留一个没有消费者的属性。 */
typedef struct {
    GPIO_TypeDef *port;
    unsigned pin;
    unsigned af;
    TIM_TypeDef *inst;
    unsigned channel;    /* 1..4 */
    uint32_t arr_max;
    uint32_t prescaler;  /* PSC = prescaler - 1，与参考工程 tpad_init(psc) 同口径 */
    uint32_t charge_ms;  /* 放电（推挽输出 0）保持时间 */
} board_rt_tpad_t;

int board_rt_tpad(board_rt_tpad_t *out);

/* CAN1（P6e-1）：引脚掩码 + AF + 目标位率 + 运行模式。
 *
 * loopback 是**运行模式**而不是"降级开关"：本板 PA11/PA12 与 USB D-/D+ 共用
 * P9 跳线，板上也没有第二个 CAN 节点，正常模式发帧只会得到"无人应答"。
 * 回环模式下 BTR.LBKM=1，CAN 核内部把 Tx 直接送进 Rx，与跳线和收发器无关，
 * 于是"发一帧、自己收到同一帧"就能证明分频/邮箱/过滤器/FIFO 全链是通的。
 * 真有对端时把 DTB 的 loopback 改 0，驱动逻辑不动。 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t mask;       /* 恰好 1~2 位：RX + TX 可以同端口（本板都在 PA） */
    unsigned af;
    uint32_t bitrate;
    int loopback;
} board_rt_can_t;

int board_rt_can(board_rt_can_t *out);

/* USART3（P6e-1）：引脚掩码 + AF + 波特率 + 判帧空闲阈值（ms）。
 * 掩码必须恰好 2 位（TX + RX）——只剩一位说明 DTS 少写了一根脚，
 * 那种配置会"发得出去、收不回来"，现场表现成"对端没回话"，最难查。 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t mask;
    unsigned af;
    uint32_t baud;
    uint32_t frame_idle_ms;
} board_rt_usart_t;

int board_rt_usart(board_rt_usart_t *out);

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

/* NAND（P9a）：页/块几何 + R/B 脚。几何来自 DTB，不写死在驱动里 ——
 * 擦写粒度错了的后果是"读得到、写进去再读变 0xFF"，比读不到更难查。 */
typedef struct {
    uint16_t page_main;
    uint16_t page_spare;
    uint32_t block_pages;
    uint32_t blocks;
    GPIO_TypeDef *rb_port;
    uint16_t rb_mask;
} board_rt_nand_t;

int board_rt_nand(board_rt_nand_t *out);
/* DMA（P9c-1）：只登记实例/流号。M2M 不需要引脚，能力由芯片固定。 */
typedef struct {
    unsigned instance;
    unsigned stream;
} board_rt_dma_t;

int board_rt_dma(board_rt_dma_t *out);

#endif /* BOARD_RT_H */
