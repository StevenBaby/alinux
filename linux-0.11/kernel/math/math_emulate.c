/*
 * linux/kernel/math/math_emulate.c
 *
 * (C) 1991 Linus Torvalds
 */

// 该目录里应该包含数学仿真代码，目前仅产生一个信号

// 信号头文件 定义信号符号常量，信号结构以及信号操作函数原型
#include <signal.h>

// 调度程序头文件，定义了任务结构 task_struct、第 1 个初始任务的数据
// 还有一些以宏的形式定义的有关描述符参数设置和获取的内联汇编函数程序
#include <linux/sched.h>

// 内核头文件 含有一些内核常用函数的原形定义
#include <linux/kernel.h>

// 段操作头文件，定义了有关段寄存器操作的嵌入式汇编函数
#include <asm/segment.h>

// 协处理器仿真函数
// 中断处理程序调用的 C 函数，参见 kernel/math/system_call.s
void math_emulate(long edi, long esi, long ebp, long sys_call_ret,
				  long eax, long ebx, long ecx, long edx,
				  unsigned short fs, unsigned short es, unsigned short ds,
				  unsigned long eip, unsigned short cs, unsigned long eflags,
				  unsigned short ss, unsigned long esp)
{
	unsigned char first, second;

	// 0x0007 表示用户代码空间

	// 选择符 0x000F 表示在局部描述符表中描述符索引值=1，即代码空间
	// 如果段寄存器 cs 不等于 0x000F
	// 则表示 cs 一定是内核代码选择符，是在内核代码空间则出错
	// 显示此时的cs:eip 值，并显示信息 “内核中需要数学仿真”，然后进入死机状态
	if (cs != 0x000F)
	{
		printk("math_emulate: %04x:%08x\n\r", cs, eip);
		panic("Math emulation needed in kernel");
	}

	// 取用户数据区堆栈数据 first 和 second
	// 显示这些数据，并给进程设置浮点异常信号 SIGFPE
	first = get_fs_byte((char *)((*&eip)++));
	second = get_fs_byte((char *)((*&eip)++));
	printk("%04x:%08x %02x %02x\n\r", cs, eip - 2, first, second);
	current->signal |= 1 << (SIGFPE - 1);
}

// 协处理器出错处理函数
// 中断处理程序调用的 C 函数，参见 kernel/math/system_call.s
void math_error(void)
{
	// 协处理器指令，(以非等待形式) 清除所有异常标志、忙标志和状态字位 7。
	__asm__("fnclex");
	if (last_task_used_math)
		last_task_used_math->signal |= 1 << (SIGFPE - 1);
}
