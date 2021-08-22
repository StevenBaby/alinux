/*
 *  linux/lib/_exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__

// Linux 标准头文件，定义了各种符号常数和类型，并申明了各种函数
// 如定义了 __LIBRARY__，则还含系统调用号和内嵌汇编 syscall0() 等
#include <unistd.h>

// 内核使用的程序(退出)终止函数
// 直接调用系统中断 int 0x80，功能号 __NR_exit
// 参数：exit_code - 退出码。
void _exit(int exit_code)
{
	__asm__ __volatile__("int $0x80" ::"a"(__NR_exit), "b"(exit_code));
}
