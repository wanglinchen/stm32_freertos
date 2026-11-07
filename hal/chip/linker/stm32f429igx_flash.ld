/*
******************************************************************************
**
** @file        : STM32F429IGTx_FLASH.ld
**
** @author      : Adapted from the STM32CubeIDE STM32F429ZITx linker script
**
** @brief       : Linker script for STM32F429IGTx Device from STM32F4 series
**                      1024Kbytes ROM
**                      64Kbytes CCMRAM
**                      192Kbytes RAM
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**                Set memory bank area and size if external memory is used
**
**  Target      : STMicroelectronics STM32
**
**  Distribution: The file is distributed as is, without any warranty
**                of any kind.
**
******************************************************************************
** @attention
**
** Copyright (c) 2022 STMicroelectronics.
** All rights reserved.
**
** This software is licensed under terms that can be found in the LICENSE file
** in the root directory of this software component.
** If no LICENSE file comes with this software, it is provided AS-IS.
**
******************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM); /* end of "RAM" Ram type memory */

_Min_Heap_Size = 0x200; /* required amount of heap */
_Min_Stack_Size = 0x400; /* required amount of stack */

/* Memories definition */
MEMORY
{
  CCMRAM    (xrw)    : ORIGIN = 0x10000000,   LENGTH = 64K
  RAM    (xrw)    : ORIGIN = 0x20000000,   LENGTH = 192K
  ROM    (rx)    : ORIGIN = 0x08000000,   LENGTH = 1024K
}

/* Sections */
SECTIONS
{
  /* The startup code into "ROM" Rom type memory */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >ROM

  /* The program code and other data into "ROM" Rom type memory */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >ROM

  /* Constant data into "ROM" Rom type memory */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >ROM

  .ARM.extab (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >ROM

  .ARM (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >ROM

  .preinit_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >ROM

  .init_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >ROM

  .fini_array (READONLY) : /* The READONLY keyword is only supported in GCC11 and later, remove it if using GCC10 or earlier. */
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >ROM

  /* Used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections into "RAM" Ram type memory */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.RamFunc)        /* .RamFunc sections */
    *(.RamFunc*)       /* .RamFunc* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */

  } >RAM AT> ROM

  /* CCM-RAM section
  *
  * 本工程当前不启用 CCMRAM：ST 模板给 .ccmram 分配了 CCMRAM 地址，但
  * startup_stm32f429xx.s 只搬 .data、只清 .bss，没有 ccmram 的拷贝/清零循环，
  * 任何 __attribute__((section(".ccmram"))) 的已初始化变量都会拿到垃圾值且链接不报错。
  *
  * 现在改为显式 DISCARD：真有人往里放变量时会在链接期直接失败，而不是悄悄出错。
  * 要启用 CCMRAM 必须同时补三件事：
  *   1) startup 里加 _siccmram -> _sccmram 的拷贝循环；
  *   2) startup 里加 _sccmram -> _eccmram 的 bss 清零（.ccmram 里的未初始化符号）；
  *   3) 恢复本节并去掉下面 /DISCARD/ 里的两条 *(.ccmram*)。
  */

  /* Uninitialized data section into "RAM" Ram type memory */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >RAM

  /* 复位不清的 RAM：panic 现场跨复位保留。
   *
   * startup_stm32f429xx.s 只清 _sbss.._ebss，本段落在 .bss 之外且是 NOLOAD，
   * 因此 Reset_Handler 不会碰它，掉电以外的复位（IWDG / NRST / 软件复位）都能
   * 把上一次的 g_panic_record 原样留在 RAM 里。上电时内容是随机的，判据是记录
   * 自身 magic（0x50414E43），不是本段地址。
   *
   * 谁改这里都要同步：create-project.py 的 verify 会断言 g_panic_record 落在
   * [ _snoinit, _enoinit ) 之内，挪回 .bss 会直接红。 */
  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    _snoinit = .;
    . = . + 8;      /* 隔离带：.bss 清零循环目前是“先比后写”（bcc），这里再留 8 B，
                       将来循环改成先写后判时也不会碰到 record 的第一个字 */
    *(.noinit)
    *(.noinit*)
    . = ALIGN(4);
    _enoinit = .;
  } >RAM

  /* User_heap_stack section, used to check that there is enough "RAM" Ram  type memory left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );

    /* newlib 的 malloc 只能从这里取内存：osl/src/freertos_port_st.c 的 _sbrk
     * 以这两个符号为界，越界直接 panic(5)。512 字节只够 printf 一类小临时，
     * 长期分配请用 FreeRTOS 的 pvPortMalloc（32 KB heap_4）。 */
    __heap_start__ = .;
    . = . + _Min_Heap_Size;
    __heap_end__ = .;

    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* Remove information from the compiler libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
    *(.ccmram)
    *(.ccmram*)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
