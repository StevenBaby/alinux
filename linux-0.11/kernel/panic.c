/*
 *  linux/kernel/panic.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 该函数在整个内核中使用（包括在 头文件 *.h, 内存管理程序 mm 和文件系统 fs 中）
// 用以指出主要的出错问题，然后进入死循环

#define PANIC

// 内核头文件，含有一些内核常用函数的原形定义
#include <linux/kernel.h>

// 调度程序头文件，定义了任务结构 task_struct、初始任务 0 的数据
// 还有一些有关描述符参数设置和获取的内联汇编函数宏语句
#include <linux/sched.h>

// 实际上是整型 int fs/buffer.c
void sys_sync(void);

// 该函数用来显示内核中出现的重大错误信息
// 并运行文件系统同步函数，然后进入死循环 / 死机
// 如果当前进程是任务 0 的话，还说明是交换任务出错
// 并且还没有运行文件系统同步函数
volatile void panic(const char *s)
{
	printk("Kernel panic: %s\n\r", s);
	if (current == task[0])
		printk("In swapper task - not syncing\n\r");
	else
		sys_sync();
	for (;;)
		;
}
