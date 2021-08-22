/*
 *  linux/lib/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <stdarg.h>

// 打开文件函数。
// 打开并有可能创建一个文件
// 参数：filename - 文件名；flag - 文件打开标志；...
// 返回：文件描述符，若出错则置出错码，并返回 -1
int open(const char *filename, int flag, ...)
{
	register int res;
	va_list arg;
	// 利用 va_start() 宏函数，取得 flag 后面参数的指针
	// 然后调用系统中断 int 0x80，功能 open 进行文件打开操作
	// %0 - eax(返回的描述符或出错码)；
	// %1 - eax(系统中断调用功能号__NR_open)；
	// %2 - ebx(文件名filename)；
	// %3 - ecx(打开文件标志flag)；
	// %4 - edx(后随参数文件属性mode)。
	va_start(arg, flag);
	__asm__("int $0x80"
			: "=a"(res)
			: "0"(__NR_open), "b"(filename), "c"(flag),
			  "d"(va_arg(arg, int)));

	// 系统中断调用返回值大于或等于 0，表示是一个文件描述符，则直接返回之
	if (res >= 0)
		return res;

	// 否则说明返回值小于 0，则代表一个出错码，设置该出错码并返回 -1
	errno = -res;
	return -1;
}
