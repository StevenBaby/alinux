/*
 *  linux/kernel/printk.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 当处于内核模式时，我们不能使用 printf，因为寄存器 fs 指向其它不感兴趣的地方
// 自己编制一个 printf 并在使用前保存 fs，一切就解决了

// 标准参数头文件。以宏的形式定义变量参数列表
// 主要说明了一个类型 (va_list) 和三个宏 (va_start, va_arg 和va_end)
// 用于 vsprintf、vprintf、vfprintf 函数
// 获取 printf 结构字符串中的可变参数
#include <stdarg.h>

// 标准定义头文件。定义了 NULL, offsetof(TYPE, MEMBER)
#include <stddef.h>

// 内核头文件，含有一些内核常用函数的原形定义
#include <linux/kernel.h>

// 输出缓冲区
static char buf[1024];

// 下面该函数 vsprintf() 在 linux/kernel/vsprintf.c
extern int vsprintf(char *buf, const char *fmt, va_list args);

// 内核使用的显示函数
int printk(const char *fmt, ...)
{
	// va_list 实际上是一个字符指针类型
	va_list args;
	int i;

	// 参数处理开始函数。在 stdarg.h
	va_start(args, fmt);

	// 使用格式串 fmt 将参数列表 args 输出到 buf 中
	// 返回值 i 等于输出字符串的长度
	i = vsprintf(buf, fmt, args);

	// 参数处理结束函数
	va_end(args);

	__asm__("push %%fs\n\t"		 // 保存 fs
			"push %%ds\n\t"		 // 压入 ds
			"pop %%fs\n\t"		 // 弹出 ds 到 fs，也即 令 fs = ds
			"pushl %0\n\t"		 // 将字符串长度压入堆栈(这三个入栈是调用参数)
			"pushl $buf\n\t"	 // 将 buf 的地址压入堆栈
			"pushl $0\n\t"		 // 将数值 0 压入堆栈，是通道号 channel
			"call tty_write\n\t" // 调用 tty_write 函数
			"addl $8,%%esp\n\t"	 // 调用完成恢复栈
			"popl %0\n\t"		 // 弹出字符串长度值，作为返回值
			"pop %%fs"			 // 恢复原fs 寄存器
			:					 // 没有输出参数
			: "r"(i)			 // 输入参数为 i
			: "ax", "cx", "dx"); // 通知编译器 ax, cx, dx 可能已经改变

	//返回输出字符串长度
	return i;
}
