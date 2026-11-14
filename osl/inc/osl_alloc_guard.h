/* osl_alloc_guard.h -- 编译期禁用 newlib 的分配入口
 *
 * 本工程只有一个堆：FreeRTOS 的 heap_4（configTOTAL_HEAP_SIZE = 16 KB，ucHeap
 * 是 .bss 里的静态数组）。newlib 侧只有链接脚本里的 _Min_Heap_Size = 512 B，
 * 这个边界就是唯一的墙：__heap_end__ 到 _estack 之间是约 175 KB 空闲 RAM（实测
 * __heap_end__ = 0x200043e8、_estack = 0x20030000），而栈是从 _estack 向下长的，
 * 所以大块 malloc 越界后既不会撞到栈、也不会报错，链接期同样看不出来 —— 越界只能靠
 * _sbrk 自己的边界判断挡住（见 osl/src/osl_boot.c）。
 *
 * 以前这里写的是“__heap_end__ 之后就是 ._user_heap_stack 的栈区”，那是把段声明
 * 的意向当成了实际布局：栈实际在 175 KB 之外，中间既无保护也无检测。
 *
 * 所以这里把 newlib 的分配入口在编译期毒掉：谁在 .c 里写 malloc / calloc /
 * realloc / free / strdup，编译直接失败，逼他改用 pvPortMalloc / vPortFree。
 *
 * 两个实现细节：
 * 1) 必须先包含 <stdlib.h> / <string.h> 再下毒。顺序反过来会在系统头里 malloc
 *    的声明处就报 attempt to use poisoned，报错位置和真正的原因对不上
 *    （实测 arm-none-eabi-gcc 13.3.1：先 poison 后 include <stdlib.h>，
 *    错误指向 stdlib.h:108 的声明行）。
 * 2) 本头由 toolchain.make 的 CFLAGS 用 -include 注入（write_toolchain() 生成），
 *    因此对 apl / fml / bsl / osl / hal 全部生效，不依赖谁记得手写 #include。
 *    只加进 CFLAGS / CXXFLAGS，不加 ASFLAGS。
 *
 * 局限：这是词法级检查，管不到已经编进 libc_nano.a 内部的调用。产物级还有一道
 * 门禁：build/build.py verify 的 collect_artifact_errors() 会断言镜像里不许出现
 * _malloc_r / _calloc_r / _realloc_r，并核对 __heap_start__/__heap_end__ 的
 * 实际间距与链接脚本的 _Min_Heap_Size 一致。
 */
#ifndef OSL_ALLOC_GUARD_H
#define OSL_ALLOC_GUARD_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC poison malloc calloc realloc free strdup

#endif /* OSL_ALLOC_GUARD_H */
