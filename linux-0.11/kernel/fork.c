/*
 *  linux/kernel/fork.c
 *
 *  (C) 1991  Linus Torvalds
 */

// fork.c 中含有系统调用 fork 的辅助子程序，参见 system_call.s
// 以及一些其它函数 verify_area，
// 一旦你了解了 fork，就会发现它是非常简单的，但内存管理却有些难度
// 参见 mm/mm.c 中的 copy_page_tables()

// 错误号头文件 包含系统中各种出错号
#include <errno.h>

// 调度程序头文件，定义了任务结构 task_struct、初始任务 0 的数据
// 还有一些有关描述符参数设置和获取的内联汇编函数宏语句
#include <linux/sched.h>

// 内核头文件，含有一些内核常用函数的原形定义
#include <linux/kernel.h>

// 段操作头文件。定义了有关段寄存器操作的内联汇编函数
#include <asm/segment.h>

// 系统头文件，定义了设置或修改描述符/中断门等的内联汇编宏
#include <asm/system.h>

extern void write_verify(unsigned long address);

// 最新进程号，其值由 get_empty_process() 生成
long last_pid = 0;

// 进程空间区域写前验证函数
// 对当前进程地址从 addr 到 addr + size 这一段空间以页为单位执行写操作前的检测操作
// 由于检测判断是以页面为单位进行操作，因此程序首先需要找出 addr 所在页面开始地址 start，
// 然后 start 加上进程数据段基址，使这个 start 变换成 CPU 4G 线性空间中的地址
// 最后循环调用 write_verify() 对指定大小的内存空间进行写前验证
// 若页面是只读的，则执行共享检验和复制页面操作（写时复制）
void verify_area(void *addr, int size)
{
	unsigned long start;

	start = (unsigned long)addr;

	// 将起始地址start 调整为其所在页的左边界开始位置，同时相应地调整验证区域大小
	// 下句中的 start & 0xfff 用来获得指定起始位置 addr（也即start）在所在页面中的偏移值，
	// 原验证范围 size 加上这个偏移值即扩展成以 addr 所在页面起始位置开始的范围值
	// 因此下面也需要把验证开始位置 start 调整成页面边界值
	size += start & 0xfff;
	start &= 0xfffff000;

	// 下面把 start 加上进程数据段在线性地址空间中的起始基址，
	// 变成系统整个线性空间中的地址位置
	// 对于 0.11 内核，其数据段和代码段在线性地址空间中的基址和限长均相同
	start += get_base(current->ldt[2]);
	while (size > 0)
	{
		size -= 4096;
		// 写页面验证，若页面不可写，则复制页面
		write_verify(start);
		start += 4096;
	}
}

// 设置新任务的代码和数据段基址、限长并复制页表
// nr 为新任务号；p 是新任务数据结构的指针
int copy_mem(int nr, struct task_struct *p)
{
	unsigned long old_data_base, new_data_base, data_limit;
	unsigned long old_code_base, new_code_base, code_limit;

	// 取局部描述符表中代码段描述符项中段限长
	code_limit = get_limit(0x0f);

	// 取局部描述符表中数据段描述符项中段限长
	data_limit = get_limit(0x17);

	// 取原代码段基址
	old_code_base = get_base(current->ldt[1]);

	// 取原数据段基址
	old_data_base = get_base(current->ldt[2]);

	// 0.11 版不支持代码和数据段分立的情况
	if (old_data_base != old_code_base)
		panic("We don't support separate I&D");

	// 如果数据段长度 < 代码段长度也不对
	if (data_limit < code_limit)
		panic("Bad data_limit");

	// 创建中新进程在线性地址空间中的基地址等于 64MB * 其任务号
	new_data_base = new_code_base = nr * 0x4000000;
	p->start_code = new_code_base;

	// 设置代码段描述符中基址域
	set_base(p->ldt[1], new_code_base);

	// 设置数据段描述符中基址域
	set_base(p->ldt[2], new_data_base);

	// 设置新进程的页目录表项和页表项
	// 即把新进程的线性地址内存页，对应到实际物理地址内存页面上
	// 复制代码和数据段
	if (copy_page_tables(old_data_base, new_data_base, data_limit))
	{
		// 如果出错则释放申请的内存
		printk("free_page_tables: from copy_mem\n");
		free_page_tables(new_data_base, data_limit);
		return -ENOMEM;
	}
	return 0;
}

// OK，下面是主要的fork 子程序
// 它复制系统进程信息(task[n])并且设置必要的寄存器。
// 它还整个地复制数据段

// 复制进程。
// 其中参数 nr 是调用 find_empty_process() 分配的任务数组项号
// none 是 system_call.s 中调用 sys_call_table 时压入堆栈的返回地址
int copy_process(int nr, long ebp, long edi, long esi, long gs, long none,
				 long ebx, long ecx, long edx,
				 long fs, long es, long ds,
				 long eip, long cs, long eflags, long esp, long ss)
{
	struct task_struct *p;
	int i;
	struct file *f;

	// 为新任务数据结构分配内存
	p = (struct task_struct *)get_free_page();

	// 如果内存分配出错，则返回出错码并退出
	if (!p)
		return -EAGAIN;

	// 将新任务结构指针放入任务数组中
	// 其中 nr 为任务号，由前面 find_empty_process() 返回
	task[nr] = p;

	// 注意！这样做不会复制超级用户的堆栈 （只复制当前进程内容）
	*p = *current;

	// 将新进程的状态先置为不可中断等待状态
	p->state = TASK_UNINTERRUPTIBLE;

	// 新进程号，由前面调用 find_empty_process() 得到
	p->pid = last_pid;

	// 设置父进程号
	p->father = current->pid;
	p->counter = p->priority;

	// 信号位图置 0
	p->signal = 0;

	// 报警定时值（滴答数）
	p->alarm = 0;

	// 进程的领导权是不能继承的
	p->leader = 0;

	// 初始化用户态时间和核心态时间
	p->utime = p->stime = 0;

	// 初始化子进程用户态和核心态时间
	p->cutime = p->cstime = 0;

	// 当前滴答数时间
	p->start_time = jiffies;

	// 以下设置任务状态段 TSS 所需的数据
	p->tss.back_link = 0;

	// 由于是给任务结构 p 分配了 1 页新内存
	// 所以此时 esp0 正好指向该页顶端
	// ss0:esp0 用于作为程序在内核态执行时的堆栈

	// 内核态堆栈指针
	p->tss.esp0 = PAGE_SIZE + (long)p;

	// 内核堆栈段选择符
	p->tss.ss0 = 0x10;

	// 指令代码指针
	p->tss.eip = eip;

	// 标志寄存器
	p->tss.eflags = eflags;

	// 这是当 fork() 返回时，新进程会返回 0 的原因所在
	p->tss.eax = 0;
	p->tss.ecx = ecx;
	p->tss.edx = edx;
	p->tss.ebx = ebx;

	// 新进程完全复制了父进程的堆栈内容
	// 因此要求 task0 的堆栈比较干净
	p->tss.esp = esp;
	p->tss.ebp = ebp;
	p->tss.esi = esi;
	p->tss.edi = edi;

	// 段寄存器仅 16 位有效
	p->tss.es = es & 0xffff;
	p->tss.cs = cs & 0xffff;
	p->tss.ss = ss & 0xffff;
	p->tss.ds = ds & 0xffff;
	p->tss.fs = fs & 0xffff;
	p->tss.gs = gs & 0xffff;

	// 设置新任务的局部描述符表的选择符 (LDT 描述符在 GDT 中)
	p->tss.ldt = _LDT(nr);
	p->tss.trace_bitmap = 0x80000000;

	// 如果当前任务使用了协处理器，就保存其上下文
	// 汇编指令 clts 用于清除控制寄存器 CR0 中的任务已交换 (TS) 标志
	// 每当发生任务切换，CPU 都会设置该标志。该标志用于管理数学协处理器
	// 如果该标志置位，那么每个 ESC 指令都会被捕获
	// 如果协处理器存在标志也同时置位的话那么就会捕获 WAIT 指令
	// 因此，如果任务切换发生在一个 ESC 指令开始执行之后
	// 则协处理器中的内容就可能需要在执行新的 ESC 指令之前保存起来
	// 错误处理句柄会保存协处理器的内容并复位 TS 标志
	// 指令 fnsave 用于把协处理器的所有状态保存到目的操作数指定的内存区域中 (tss.i387)
	if (last_task_used_math == current)
		__asm__("clts \n fnsave %0" ::"m"(p->tss.i387));

	// 设置新任务的代码和数据段基址、限长并复制页表
	// 如果出错（返回值不是0），则复位任务数组中相应项
	// 并释放为该新任务分配的内存页
	if (copy_mem(nr, p))
	{
		task[nr] = NULL;
		free_page((long)p);
		return -EAGAIN;
	}

	// 如果父进程中有文件是打开的，则将对应文件的打开次数增 1
	for (i = 0; i < NR_OPEN; i++)
		if ((f = p->filp[i]))
			f->f_count++;
	// 将当前进程（父进程）的 pwd, root 和 executable 引用次数均增 1
	if (current->pwd)
		current->pwd->i_count++;
	if (current->root)
		current->root->i_count++;
	if (current->executable)
		current->executable->i_count++;

	// 在 GDT 中设置新任务的 TSS 和 LDT 描述符项，数据从 task 结构中取
	// 在任务切换时，任务寄存器 tr 由 CPU 自动加载
	set_tss_desc(gdt + (nr << 1) + FIRST_TSS_ENTRY, &(p->tss));
	set_ldt_desc(gdt + (nr << 1) + FIRST_LDT_ENTRY, &(p->ldt));

	// 最后再将新任务设置成可运行状态，以防万一
	p->state = TASK_RUNNING;

	// 返回新进程号（与任务号是不同的）
	return last_pid;
}

// 为新进程取得不重复的进程号 last_pid，
// 并返回在任务数组中的任务号 (数组 index)
int find_empty_process(void)
{
	int i;

repeat:
	// 如果 last_pid 增 1 后超出其正数表示范围
	// 则重新从 1 开始使用 pid 号
	if ((++last_pid) < 0)
		last_pid = 1;

	// 在任务数组中搜索刚设置的 pid 号是否已经被任何任务使用
	// 如果是则重新获得一个 pid 号
	for (i = 0; i < NR_TASKS; i++)
		if (task[i] && task[i]->pid == last_pid)
			goto repeat;

	// 在任务数组中为新任务寻找一个空闲项，并返回项号
	// last_pid 是一个全局变量，不用返回
	for (i = 1; i < NR_TASKS; i++)
		if (!task[i])
			return i;

	// 如果任务数组中64 个项已经被全部占用，则返回出错码
	return -EAGAIN;
}
