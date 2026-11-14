/**
 * @file system_stm32f4xx.c
 * @brief STM32F429 CMSIS 启动：SystemInit、SystemClock_Config、SystemCoreClockUpdate。
 *
 * 本仓库仅面向 STM32F429；外扩 FMC 初始化沿用 ST F429 走线，由 DATA_IN_Ext* 宏选用。
 */

#include "stm32f4xx.h"

#ifndef STM32F429xx
#error "system_stm32f4xx.c in this tree is for STM32F429xx only"
#endif

#if !defined(HSE_VALUE)
#define HSE_VALUE ((uint32_t)25000000)
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE ((uint32_t)16000000)
#endif

/* 片外作 .data/.bss/堆栈时打开；须与链接脚本一致（阿波罗板为 SDRAM，见原理图 MEMORY 页）。 */
/* #define DATA_IN_ExtSRAM */
/* #define DATA_IN_ExtSDRAM */

/* #define USER_VECT_TAB_ADDRESS */
#if defined(USER_VECT_TAB_ADDRESS)
/* #define VECT_TAB_SRAM */
#if defined(VECT_TAB_SRAM)
#define VECT_TAB_BASE_ADDRESS SRAM_BASE
#else
#define VECT_TAB_BASE_ADDRESS FLASH_BASE
#endif
#if !defined(VECT_TAB_OFFSET)
#define VECT_TAB_OFFSET 0x00000000U
#endif
#endif

#define VOS_READY_GUARD 0x000FFFFFU

/* g_boot_stage==2 时再用子步区分 VOS / HSE·PLL / 切 SYSCLK（.noinit，调试器直读）。 */
volatile uint32_t g_clock_fail_sub __attribute__((section(".noinit")));
volatile uint32_t g_clock_fail_rcc_cr __attribute__((section(".noinit")));

uint32_t SystemCoreClock = 16000000;

static void clock_fail_note(uint32_t sub)
{
    g_clock_fail_sub = sub;
    g_clock_fail_rcc_cr = RCC->CR;
}

static void clock_reg_settle_ms(uint32_t ms)
{
    uint32_t start;
    uint32_t limit;

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) {
        return;
    }
    if (SystemCoreClock < 1000U) {
        return;
    }
    start = DWT->CYCCNT;
    limit = (SystemCoreClock / 1000U) * ms;
    while ((DWT->CYCCNT - start) < limit) {
    }
}
const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8] = {0, 0, 0, 0, 1, 2, 3, 4};

#if defined(DATA_IN_ExtSRAM) || defined(DATA_IN_ExtSDRAM)
static void SystemInit_ExtMemCtl(void);
#endif

void SystemInit(void)
{
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << 10 * 2) | (3UL << 11 * 2));
#endif

#if defined(DATA_IN_ExtSRAM) || defined(DATA_IN_ExtSDRAM)
    SystemInit_ExtMemCtl();
#endif

#if defined(USER_VECT_TAB_ADDRESS)
    SCB->VTOR = VECT_TAB_BASE_ADDRESS | VECT_TAB_OFFSET;
#endif
}

/**
 * HSE 25 MHz → PLL → HCLK 168 MHz（APB1 42 MHz / APB2 84 MHz）；见 stm32f4xx_hal_conf.h、
 * doc/hardware/sch/STM32F429_CORE_V2.pdf。须在 .data/.bss 初始化之后调用。
 * @retval 0 成功；1 失败，保持 HSI 16 MHz
 */
int SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    volatile uint32_t guard;

    g_clock_fail_sub = 0U;
    g_clock_fail_rcc_cr = 0U;

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    clock_reg_settle_ms(2U);
    guard = VOS_READY_GUARD;
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0U) {
        if (guard == 0U) {
            /* PWR_CSR.VOSRDY 在部分 F429 核心板上长期不置位，但 VDD≈3.2 V 仍可跑 168 MHz。 */
            clock_fail_note(1U);
            break;
        }
        guard--;
    }

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 25U;
    osc.PLL.PLLN = 336U;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7U;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        clock_fail_note(2U);
        return 1;
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        clock_fail_note(3U);
        return 1;
    }

    return 0;
}

/* 不得在此保留 weak main：会破坏 libapl.a 强符号链接，见 .skills/工程规格.md */

void SystemCoreClockUpdate(void)
{
    uint32_t tmp, pllvco, pllp, pllsource, pllm;

    tmp = RCC->CFGR & RCC_CFGR_SWS;

    switch (tmp) {
    case 0x00:
        SystemCoreClock = HSI_VALUE;
        break;
    case 0x04:
        SystemCoreClock = HSE_VALUE;
        break;
    case 0x08:
        pllsource = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) >> 22;
        pllm = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;
        if (pllsource != 0) {
            pllvco = (HSE_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        } else {
            pllvco = (HSI_VALUE / pllm) * ((RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> 6);
        }
        pllp = (((RCC->PLLCFGR & RCC_PLLCFGR_PLLP) >> 16) + 1) * 2;
        SystemCoreClock = pllvco / pllp;
        break;
    default:
        SystemCoreClock = HSI_VALUE;
        break;
    }

    tmp = AHBPrescTable[(RCC->CFGR & RCC_CFGR_HPRE) >> 4];
    SystemCoreClock >>= tmp;
}

#if defined(DATA_IN_ExtSRAM) || defined(DATA_IN_ExtSDRAM)

#if defined(DATA_IN_ExtSDRAM)
static void SystemInit_ExtMemCtl_SdramInit(void)
{
    register uint32_t tmpreg = 0;
    register uint32_t timeout = 0xFFFFU;
    register __IO uint32_t index;

    FMC_Bank5_6->SDCR[0] = 0x000019E4;
    FMC_Bank5_6->SDTR[0] = 0x01115351;

    FMC_Bank5_6->SDCMR = 0x00000011;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020U;
    while ((tmpreg != 0U) && (timeout-- > 0U)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020U;
    }

    for (index = 0U; index < 1000U; index++) {
    }

    FMC_Bank5_6->SDCMR = 0x00000012;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020U;
    timeout = 0xFFFFU;
    while ((tmpreg != 0U) && (timeout-- > 0U)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020U;
    }

    FMC_Bank5_6->SDCMR = 0x00000073;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020U;
    timeout = 0xFFFFU;
    while ((tmpreg != 0U) && (timeout-- > 0U)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020U;
    }

    FMC_Bank5_6->SDCMR = 0x00046014;
    tmpreg = FMC_Bank5_6->SDSR & 0x00000020U;
    timeout = 0xFFFFU;
    while ((tmpreg != 0U) && (timeout-- > 0U)) {
        tmpreg = FMC_Bank5_6->SDSR & 0x00000020U;
    }

    tmpreg = FMC_Bank5_6->SDRTR;
    FMC_Bank5_6->SDRTR = (tmpreg | (0x0000027CU << 1));

    tmpreg = FMC_Bank5_6->SDCR[0];
    FMC_Bank5_6->SDCR[0] = (tmpreg & 0xFFFFFDFFU);
}
#endif

static void SystemInit_ExtMemCtl_GpioFmc(void)
{
#if defined(DATA_IN_ExtSRAM) && defined(DATA_IN_ExtSDRAM)
    GPIOD->AFR[0] = 0x00CCC0CC;
    GPIOD->AFR[1] = 0xCCCCCCCC;
    GPIOD->MODER = 0xAAAA0A8A;
    GPIOD->OSPEEDR = 0xFFFF0FCF;
    GPIOD->OTYPER = 0x00000000;
    GPIOD->PUPDR = 0x00000000;

    GPIOE->AFR[0] = 0xC00CC0CC;
    GPIOE->AFR[1] = 0xCCCCCCCC;
    GPIOE->MODER = 0xAAAA828A;
    GPIOE->OSPEEDR = 0xFFFFC3CF;
    GPIOE->OTYPER = 0x00000000;
    GPIOE->PUPDR = 0x00000000;

    GPIOF->AFR[0] = 0xCCCCCCCC;
    GPIOF->AFR[1] = 0xCCCCCCCC;
    GPIOF->MODER = 0xAA800AAA;
    GPIOF->OSPEEDR = 0xAA800AAA;
    GPIOF->OTYPER = 0x00000000;
    GPIOF->PUPDR = 0x00000000;

    GPIOG->AFR[0] = 0xCCCCCCCC;
    GPIOG->AFR[1] = 0xCCCCCCCC;
    GPIOG->MODER = 0xAAAAAAAA;
    GPIOG->OSPEEDR = 0xAAAAAAAA;
    GPIOG->OTYPER = 0x00000000;
    GPIOG->PUPDR = 0x00000000;
#elif defined(DATA_IN_ExtSDRAM)
    GPIOD->AFR[0] = 0x000000CC;
    GPIOD->AFR[1] = 0xCC000CCC;
    GPIOD->MODER = 0xA02A000A;
    GPIOD->OSPEEDR = 0xA02A000A;
    GPIOD->OTYPER = 0x00000000;
    GPIOD->PUPDR = 0x00000000;

    GPIOE->AFR[0] = 0xC00000CC;
    GPIOE->AFR[1] = 0xCCCCCCCC;
    GPIOE->MODER = 0xAAAA800A;
    GPIOE->OSPEEDR = 0xAAAA800A;
    GPIOE->OTYPER = 0x00000000;
    GPIOE->PUPDR = 0x00000000;

    GPIOF->AFR[0] = 0xCCCCCCCC;
    GPIOF->AFR[1] = 0xCCCCCCCC;
    GPIOF->MODER = 0xAA800AAA;
    GPIOF->OSPEEDR = 0xAA800AAA;
    GPIOF->OTYPER = 0x00000000;
    GPIOF->PUPDR = 0x00000000;

    GPIOG->AFR[0] = 0xCCCCCCCC;
    GPIOG->AFR[1] = 0xCCCCCCCC;
    GPIOG->MODER = 0xAAAAAAAA;
    GPIOG->OSPEEDR = 0xAAAAAAAA;
    GPIOG->OTYPER = 0x00000000;
    GPIOG->PUPDR = 0x00000000;
#elif defined(DATA_IN_ExtSRAM)
    GPIOD->AFR[0] = 0x00CCC0CC;
    GPIOD->AFR[1] = 0xCCCCCCCC;
    GPIOD->MODER = 0xAAAA0A8A;
    GPIOD->OSPEEDR = 0xFFFF0FCF;
    GPIOD->OTYPER = 0x00000000;
    GPIOD->PUPDR = 0x00000000;

    GPIOE->AFR[0] = 0xC00CC0CC;
    GPIOE->AFR[1] = 0xCCCCCCCC;
    GPIOE->MODER = 0xAAAA828A;
    GPIOE->OSPEEDR = 0xFFFFC3CF;
    GPIOE->OTYPER = 0x00000000;
    GPIOE->PUPDR = 0x00000000;

    GPIOF->AFR[0] = 0x00CCCCCC;
    GPIOF->AFR[1] = 0xCCCC0000;
    GPIOF->MODER = 0xAA000AAA;
    GPIOF->OSPEEDR = 0xFF000FFF;
    GPIOF->OTYPER = 0x00000000;
    GPIOF->PUPDR = 0x00000000;

    GPIOG->AFR[0] = 0x00CCCCCC;
    GPIOG->AFR[1] = 0x000000C0;
    GPIOG->MODER = 0x00085AAA;
    GPIOG->OSPEEDR = 0x000CAFFF;
    GPIOG->OTYPER = 0x00000000;
    GPIOG->PUPDR = 0x00000000;
#endif

#if defined(DATA_IN_ExtSDRAM)
    GPIOH->AFR[0] = 0x00C0CC00;
    GPIOH->AFR[1] = 0xCCCCCCCC;
    GPIOH->MODER = 0xAAAA08A0;
    GPIOH->OSPEEDR = 0xAAAA08A0;
    GPIOH->OTYPER = 0x00000000;
    GPIOH->PUPDR = 0x00000000;

    GPIOI->AFR[0] = 0xCCCCCCCC;
    GPIOI->AFR[1] = 0x00000CC0;
    GPIOI->MODER = 0x0028AAAA;
    GPIOI->OSPEEDR = 0x0028AAAA;
    GPIOI->OTYPER = 0x00000000;
    GPIOI->PUPDR = 0x00000000;
#endif
}

static void SystemInit_ExtMemCtl(void)
{
    __IO uint32_t tmp = 0U;

#if defined(DATA_IN_ExtSDRAM)
    RCC->AHB1ENR |= 0x000001F8U;
#else
    RCC->AHB1ENR |= 0x00000078U;
#endif
    tmp = READ_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIODEN);

    SystemInit_ExtMemCtl_GpioFmc();

    RCC->AHB3ENR |= 0x00000001U;
    tmp = READ_BIT(RCC->AHB3ENR, RCC_AHB3ENR_FMCEN);

#if defined(DATA_IN_ExtSDRAM)
    SystemInit_ExtMemCtl_SdramInit();
#endif

#if defined(DATA_IN_ExtSRAM)
    FMC_Bank1->BTCR[2] = 0x00001011;
    FMC_Bank1->BTCR[3] = 0x00000201;
    FMC_Bank1E->BWTR[2] = 0x0FFFFFFF;
#endif

    (void)tmp;
}

#endif /* DATA_IN_ExtSRAM || DATA_IN_ExtSDRAM */
