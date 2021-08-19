/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

// sched.c 是主要的内核文件
// 其中包括有关调度的基本函数 (sleep_on、wakeup、schedule 等)
// 一些简单的系统调用函数 (比如 getpid()，仅从当前任务中获取一个字段)

// 调度程序头文件，定义了任务结构 task_struct、第 1 个初始任务的数据
// 还有一些以宏的形式定义的有关描述符参数设置和获取的内联汇编函数程序
#include <linux/sched.h>

// 内核头文件，含有一些内核常用函数的原形定义
#include <linux/kernel.h>

// 系统调用头文件，含有 72 个系统调用 C 函数处理程序，以 sys_ 开头
#include <linux/sys.h>

// 软驱头文件，含有软盘控制器参数的一些定义
#include <linux/fdreg.h>

// 系统头文件，定义了设置或修改描述符/中断门等的内联汇编宏
#include <asm/system.h>

// io 头文件，定义硬件端口输入/输出宏汇编语句
#include <asm/io.h>

// 段操作头文件，定义了有关段寄存器操作的内联汇编函数
#include <asm/segment.h>

// 信号头文件，定义信号符号常量，sigaction 结构，操作函数原型
#include <signal.h>

// 取信号 nr 在信号位图中对应位的二进制数值。信号编号 1-32
// 比如信号 5 的位图数值 = 1 << (5-1) = 16 = 0b00010000
#define _S(nr) (1 << ((nr)-1))

// 除了SIGKILL 和 SIGSTOP 信号以外其它都是可阻塞的 (…10111111111011111111b)
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

// 显示任务号 nr 的进程号、进程状态和内核堆栈空闲字节数（大约）
void show_task(int nr, struct task_struct *p)
{
	int i, j = 4096 - sizeof(struct task_struct);

	// 显示任务号 nr 的进程号，进程状态
	printk("%d: pid=%d, state=%d, ", nr, p->pid, p->state);

	i = 0;

	// 检测指定任务数据结构以后等于 0 的字节
	while (i < j && !((char *)(p + 1))[i])
		i++;

	// 内核堆栈空闲字节数（大约）
	printk("%d (of %d) chars free in kernel stack\n\r", i, j);
}

// 显示所有任务的任务号、进程号、进程状态和内核堆栈空闲字节数（大约）
void show_stat(void)
{
	int i;

	for (i = 0; i < NR_TASKS; i++)
		if (task[i])
			show_task(i, task[i]);
}

// 定义每个时间片的滴答数 😊
#define LATCH (1193180 / HZ)

// 没有任何地方定义和引用该函数 🤔
extern void mem_use(void);

// 时钟中断处理程序 kernel/system_call.s
extern int timer_interrupt(void);

// 系统调用中断处理程序 kernel/system_call.s
extern int system_call(void);

// 定义任务联合 (任务结构成员和 stack 字符数组程序成员)
// 因为一个任务的数据结构与其内核态堆栈放在同一内存页中
// 所以从堆栈段寄存器 ss 可以获得其数据段选择符
// 每个任务（进程）在内核态运行时都有自己的内核态堆栈。这里定义了任务的内核态堆栈结构
union task_union
{
	struct task_struct task;
	char stack[PAGE_SIZE];
};

// 定义初始任务的数据(sched.h 中)。
static union task_union init_task = {
	INIT_TASK,
};

// 从开机开始算起的滴答数时间值（10ms/滴答）。
// 前面的限定符 volatile，英文解释是易变、不稳定的意思，
// 这里是要求 gcc 不要对该变量进行优化处理，也不要挪动位置
// 因为也许别的程序会来修改它的值
long volatile jiffies = 0;

// 开机时间，从1970:0:0:0 开始计时的秒数。
long startup_time = 0;

// 当前任务指针（初始化为初始任务）
struct task_struct *current = &(init_task.task);

// 使用过协处理器任务的指针
struct task_struct *last_task_used_math = NULL;

// 定义任务指针数组
struct task_struct *task[NR_TASKS] = {
	&(init_task.task),
};

// 定义用户堆栈，4K，指针指在最后一项
long user_stack[PAGE_SIZE >> 2];

// 该结构用于设置堆栈 ss:esp（数据段选择符，指针），见 head.s
struct
{
	long *a;
	short b;
} stack_start = {&user_stack[PAGE_SIZE >> 2], 0x10};

// 将当前协处理器内容保存到老协处理器状态数组中
// 并将当前任务的协处理器内容加载进协处理器

// 当任务被调度交换过以后，该函数用以保存原任务的协处理器状态（上下文）
// 并恢复新调度进来的当前任务的协处理器执行状态。

void math_state_restore()
{
	// 如果任务没变则返回 (上一个任务就是当前任务)
	// 这里所指的"上一个任务"是刚被交换出去的任务
	if (last_task_used_math == current)
		return;

	// 在发送协处理器命令之前要先发 FWAIT 指令
	__asm__("fwait");

	// 如果上个任务使用了协处理器，则保存其状态
	if (last_task_used_math)
	{
		__asm__("fnsave %0" ::"m"(last_task_used_math->tss.i387));
	}

	// 现在，last_task_used_math 指向当前任务
	// 以备当前任务被交换出去时使用
	last_task_used_math = current;

	// 如果当前任务用过协处理器，则恢复其状态
	if (current->used_math)
	{
		__asm__("frstor %0" ::"m"(current->tss.i387));
	}
	else
	{
		// 否则的话说明是第一次使用，于是就向协处理器发初始化命令
		__asm__("fninit" ::);

		// 并设置使用了协处理器标志
		current->used_math = 1;
	}
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */

// schedule() 是调度函数。这是个很好的代码！没有任何理由对它进行修改
// 因为它可以在所有的环境下工作 (比如能够对 IO 边界处理很好的响应等)
// 只有一件事值得留意，那就是这里的信号处理代码

// 注意！！0 号任务 是个空闲('idle') 任务，只有当没有其它任务可以运行时才调用它
// 它不能被杀死，也不能睡眠。任务 0 中的状态信息 'state' 是从来不用的
// Windows 中也有 pid=0 的任务，主要作用就是所有其他进程都阻塞时，执行空闲任务
// 空闲任务一般很简单，开中断，关闭 CPU，等待时钟中断的到来，所以空闲时 CPU 就不发热了

void schedule(void)
{
	int i, next, c;

	// 任务结构指针的指针
	struct task_struct **p;

	// 检测 alarm（进程的闹钟定时值），唤醒任何已得到信号的可中断任务

	// 从任务数组中最后一个任务开始检测 alarm
	for (p = &LAST_TASK; p > &FIRST_TASK; --p)
		if (*p)
		{
			// 如果设置过任务的定时值 alarm，并且已经过期 (alarm < jiffies)
			// 则在信号位图中置 SIGALRM 信号，即向任务发送 SIGALARM 信号
			// 然后清除 alarm，该信号的默认操作是终止进程，
			if ((*p)->alarm && (*p)->alarm < jiffies)
			{
				(*p)->signal |= (1 << (SIGALRM - 1));
				(*p)->alarm = 0;
			}

			// 如果信号位图中除被阻塞的信号外还有其它信号，
			// 并且任务处于可中断状态，则置任务为就绪状态
			// 其中 ~(_BLOCKABLE & (*p)->blocked) 用于忽略被阻塞的信号
			// 但 SIGKILL 和 SIGSTOP 不能被阻塞
			if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&
				(*p)->state == TASK_INTERRUPTIBLE)
				// 置为就绪（可执行）状态
				(*p)->state = TASK_RUNNING;
		}

	// 这里是调度程序的主要部分

	while (1)
	{
		c = -1;
		next = 0;
		i = NR_TASKS;
		p = &task[NR_TASKS];
		// 这段代码也是从任务数组的最后一个任务开始循环处理，并跳过不含任务的数组槽。
		// 比较每个就绪状态任务的 counter（任务运行时间的递减滴答计数）值，
		// 哪一个值大，运行时间还不长，next 就指向哪个的任务号
		while (--i)
		{
			if (!*--p)
				continue;
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
				c = (*p)->counter, next = i;
		}
		// 如果比较得出有 counter 值大于 0 的结果，则退出循环，执行任务切换
		if (c)
			break;

		// 否则所有进程的时间片都用完了
		// 就根据每个任务的优先权值，更新每一个任务的 counter 值，然后重新比较
		// counter 值的计算方式为 counter = counter / 2 + priority
		// 这里计算过程不考虑进程的状态
		for (p = &LAST_TASK; p > &FIRST_TASK; --p)
			if (*p)
				(*p)->counter = ((*p)->counter >> 1) +
								(*p)->priority;
	}
	// 切换到任务号为 next 的任务运行，next 被初始化为 0
	// 因此若系统中没有任何其它任务可运行时，则 next 始终为 0
	// 因此调度函数会在系统空闲时去执行任务 0
	// 此时任务0 仅执行 pause() 系统调用，并又会调用本函数，在 init/main.c
	switch_to(next);
}

// pause() 系统调用，转换当前任务的状态为可中断的等待状态，并重新调度
// 该系统调用将导致进程进入睡眠状态，直到收到一个信号
// 该信号用于终止进程，或者使进程调用一个信号捕获函数
// 只有当捕获了一个信号，并且信号捕获处理函数返回 pause() 才会返回
// 此时 pause() 返回值应该是 -1，并且 errno 被置为 EINTR。这里还没有完全实现（直到0.95 版）
int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return 0;
}

// 把当前任务置为不可中断的等待状态，并让睡眠队列头的指针指向当前任务
// 只有明确地唤醒时才会返回，该函数提供了进程与中断处理程序之间的同步机制
// 函数参数 *p 是放置等待任务的队列头指针
void sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	// 若指针无效，则退出（指针所指的对象可以是 NULL，但指针本身不会为 0)
	if (!p)
		return;

	// 如果当前任务是任务0，则死机，这是不可能的，一定是其他什么地方错了！！！
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");

	// 让 tmp 指向已经在等待队列上的任务(如果有的话)
	tmp = *p;

	// 将睡眠队列头的等待指针指向当前任务
	*p = current;

	// 将当前任务置为不可中断的等待状态
	current->state = TASK_UNINTERRUPTIBLE;

	// 重新调度
	schedule();

	// 只有当这个等待任务被唤醒时，调度程序才又返回到这里，则表示进程已被明确地唤醒
	// 既然大家都在等待同样的资源，那么在资源可用时，就有必要唤醒所有等待该资源的进程
	// 该函数嵌套调用，也会嵌套唤醒所有等待该资源的进程
	if (tmp)
		// 若在其前还存在等待的任务，则也将其置为就绪状态（唤醒）
		tmp->state = 0;
}

// 将当前任务置为可中断的等待状态，并放入 *p 指定的等待队列中
void interruptible_sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	// 若指针无效，则退出
	if (!p)
		return;

	// 如果当前任务是任务0，则死机
	if (current == &(init_task.task))
		panic("task[0] trying to sleep");

	// 让 tmp 指向已经在等待队列上的任务(如果有的话)
	tmp = *p;

	// 将睡眠队列头的等待指针指向当前任务
	*p = current;
repeat:
	// 将当前任务置为可中断的等待状态
	current->state = TASK_INTERRUPTIBLE;

	// 重新调度
	schedule();

	// 如果等待队列中还有等待任务，并且队列头指针所指向的任务不是当前任务时
	// 则将该等待任务置为可运行的就绪状态，并重新执行调度程序
	// 当指针 *p 所指向的不是当前任务时，表示在当前任务被放入队列后
	// 又有新的任务被插入等待队列中，因此，就应该同时也将所有其它的等待任务置为可运行状态
	if (*p && *p != current)
	{
		(**p).state = 0;
		goto repeat;
	}

	// 下面一句代码有误，应该是 *p = tmp，让队列头指针指向其余等待任务
	// 否则在当前任务之前插入等待队列的任务均被抹掉了
	// 当然，同时也需删除 wake_up 中的语句
	// TODO: 😔 《注释》说明的错误，暂不修正，我试过了，好像可以，但又怕发生其他错误，暂时先这样
	*p = NULL;
	if (tmp)
		tmp->state = 0;
}

// 唤醒指定任务 *p
void wake_up(struct task_struct **p)
{
	if (p && *p)
	{
		// 置为就绪（可运行）状态
		(**p).state = 0;
		*p = NULL;
	}
}

// 好了，从这里开始是一些有关软盘的子程序，本不应该放在内核的主要部分中的
// 将它们放在这里是因为软驱需要一个时钟，而放在这里是最方便的办法

// 下面数组存放等待软驱马达启动到正常转速的进程指针，数组索引 0-3 分别对应软驱 A-D
static struct task_struct *wait_motor[4] = {NULL, NULL, NULL, NULL};

// 下面数组分别存放各软驱马达启动所需要的滴答数，程序中默认启动时间为 50 个滴答（0.5 秒）
static int mon_timer[4] = {0, 0, 0, 0};

// 下面数组分别存放各软驱在马达停转之前需维持的时间。程序中设定为 10000 个滴答（100 秒）
static int moff_timer[4] = {0, 0, 0, 0};

// 对应软驱控制器中当前数字输出寄存器（端口），该寄存器每位的定义如下：
// 位 7 ~ 4：分别控制驱动器 D-A 马达的启动，1 - 启动；0 - 关闭
// 位 3 ：1 - 允许 DMA 和中断请求；0 - 禁止 DMA 和中断请求
// 位 2 ：1 - 启动软盘控制器；0 - 复位软盘控制器
// 位 1 ~ 0：00 - 11，用于选择控制的软驱 A-D

// 这里设置初值为：允许 DMA 和中断请求、启动 FDC
unsigned char current_DOR = 0x0C;

// 指定软驱启动到正常运转状态所需等待时间
// nr / 软驱号( 0-3 )，返回值为滴答数
int ticks_to_floppy_on(unsigned int nr)
{
	// 选中软驱标志 kernel/blk_drv/floppy.c
	extern unsigned char selected;

	// 所选软驱对应数字输出寄存器中启动马达比特位
	// mask 高 4 位是各软驱启动马达标志
	unsigned char mask = 0x10 << nr;

	if (nr > 3)
		// 系统最多有 4 个软驱
		panic("floppy_on: nr>3");

	// 首先预先设置好指定软驱 nr 停转之前需要经过的时间（100 秒）
	// 然后取当前 DOR 寄存器值到临时变量 mask 中，并把指定软驱的马达启动标志置位

	// 停转维持时间 100s 很长 😔
	moff_timer[nr] = 10000;

	// 关中断
	cli();
	mask |= current_DOR;

	// 如果当前没有选择软驱，则首先复位其它软驱的选择位，然后置指定软驱选择位
	if (!selected)
	{
		mask &= 0xFC;
		mask |= nr;
	}

	// 如果数字输出寄存器的当前值与要求的值不同，则向 FDC 数字输出端口输出新值(mask)
	// 并且如果要求启动的马达还没有启动，则置相应软驱的马达启动定时器值 (HZ/2 = 0.5 秒或 50 个滴答)
	// 若已经启动，则再设置启动定时为 2 个滴答，能满足下面 do_floppy_timer() 中先递减后判断的要求
	// 执行本次定时代码的要求即可，此后更新当前数字输出寄存器变量 current_DOR
	if (mask != current_DOR)
	{
		outb(mask, FD_DOR);
		if ((mask ^ current_DOR) & 0xf0)
			mon_timer[nr] = HZ / 2;
		else if (mon_timer[nr] < 2)
			mon_timer[nr] = 2;
		current_DOR = mask;
	}

	// 开中断
	sti();
	// 最后返回启动马达所需的时间值
	return mon_timer[nr];
}

// 等待指定软驱马达启动所需的一段时间，然后返回
// 设置指定软驱的马达启动到正常转速所需的延时，然后睡眠等待
// 在定时中断过程中会一直递减判断这里设定的延时值
// 当延时到期，就会唤醒这里的等待进程
void floppy_on(unsigned int nr)
{
	// 关中断
	cli();
	// 如果马达启动定时还没到，
	// 就一直把当前进程置不可中断睡眠状态
	// 并放入等待马达运行的队列中
	while (ticks_to_floppy_on(nr))
		sleep_on(nr + wait_motor);
	// 开中断
	sti();
}

// 置关闭相应软驱马达停转定时器 (3 秒)
// 若不使用该函数明确关闭指定的软驱马达
// 则在马达开启 100 秒之后也会被关闭
void floppy_off(unsigned int nr)
{
	moff_timer[nr] = 3 * HZ;
}

// 软盘定时处理子程序，更新马达启动定时值和马达关闭停转计时值
// 该子程序会在时钟定时中断过程中被调用
// 因此系统每经过一个滴答 (10ms) 就会被调用一次
// 随时更新马达开启或停转定时器的值
// 如果某一个马达停转定时到，则将数字输出寄存器马达启动位复位
void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;

	for (i = 0; i < 4; i++, mask <<= 1)
	{
		// 如果不是 DOR 指定的马达则跳过
		if (!(mask & current_DOR))
			continue;
		if (mon_timer[i])
		{
			if (!--mon_timer[i])
				// 如果马达启动定时到则唤醒进程
				wake_up(i + wait_motor);
		}
		else if (!moff_timer[i])
		{
			// 如果马达停转定时到，
			// 则复位相应马达启动位
			current_DOR &= ~mask;

			// 并更新数字输出寄存器
			outb(current_DOR, FD_DOR);
		}
		else
			// 马达停转计时递减
			moff_timer[i]--;
	}
}

// 最多可有 64 个定时器链表 (64 个任务)
#define TIME_REQUESTS 64

// 定时器链表结构和定时器数组
static struct timer_list
{
	// 定时滴答数
	long jiffies;

	// 定时处理程序
	void (*fn)();

	// 下一个定时器
	struct timer_list *next;
} timer_list[TIME_REQUESTS], *next_timer = NULL;

// 添加定时器，输入参数为指定的定时值(滴答数)和相应的处理程序指针。
// 软盘驱动程序（floppy.c）利用该函数执行启动或关闭马达的延时操作。
// jiffies –> 以 10 毫秒计的滴答数；*fn() -> 定时时间到时执行的函数
void add_timer(long jiffies, void (*fn)(void))
{
	struct timer_list *p;

	// 如果定时处理程序指针为空，则退出
	if (!fn)
		return;

	// 关中断
	cli();

	// 如果定时值 <=0，则立刻调用其处理程序，并且该定时器不加入链表中
	if (jiffies <= 0)
		(fn)();
	else
	{
		// 从定时器数组中，找一个空闲项
		for (p = timer_list; p < timer_list + TIME_REQUESTS; p++)
			if (!p->fn)
				break;
		// 如果已经用完了定时器数组，则系统崩溃 😊
		if (p >= timer_list + TIME_REQUESTS)
			panic("No more time requests free");

		// 向定时器数据结构填入相应信息，并链入链表头
		p->fn = fn;
		p->jiffies = jiffies;
		p->next = next_timer;
		next_timer = p;

		// 链表项按定时值从小到大排序，在排序时减去排在前面需要的滴答数
		// 这样在处理定时器时只要查看链表头的第一项的定时是否到期即可
		// 🐛 这段程序好象没有考虑周全，
		// 如果新插入的定时器值 < 原来头一个定时器值时
		// 也应该将所有后面的定时值均减去新的第 1 个的定时值
		while (p->next && p->next->jiffies < p->jiffies)
		{
			p->jiffies -= p->next->jiffies;
			fn = p->fn;
			p->fn = p->next->fn;
			p->next->fn = fn;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
	}
	// 开中断
	sti();
}

// 时钟中断 C 函数处理程序，在 kernel/system_call.s 中的 timer_interrupt 中被调用
// 参数 cpl 是当前特权级 0 或 3，0 表示内核代码在执行
// 对于一个进程由于执行时间片用完时，则进行任务切换，并执行一个计时更新工作
void do_timer(long cpl)
{
	// 扬声器发声时间滴答数 kernel/chr_drv/console.c
	extern int beepcount;

	// 关闭扬声器 kernel/chr_drv/console.c
	extern void sysbeepstop(void);

	// 如果发声计数次数到，则关闭发声
	// 向0x61 口发送命令，复位位 0 和 1。位 0 控制 8253 计数器2 的工作，位 1 控制扬声器
	if (beepcount)
		if (!--beepcount)
			sysbeepstop();

	// 如果当前特权级 (cpl)为0（最高，表示是内核程序在工作）
	// 则将内核程序运行时间 stime 递增；
	// Linus 把内核程序统称为超级用户 (supervisor) 的程序，见源 system_call.s 的英文注释
	// 如果cpl > 0，则表示是一般用户程序在工作，增加 utime
	if (cpl)
		current->utime++;
	else
		current->stime++;

	// 如果有用户的定时器存在，则将链表第 1 个定时器的值减 1。如果已等于 0
	// 则调用相应的处理程序，并将该处理程序指针置为空，然后去掉该项定时器
	if (next_timer)
	{
		// next_timer 是定时器链表的头指针
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0)
		{
			// 这里插入了一个函数指针定义！！！😔
			void (*fn)(void);

			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;

			// 调用处理函数
			(fn)();
		}
	}

	// 如果当前软盘控制器 FDC 的数字输出寄存器中马达启动位有置位的，则执行软盘定时程序
	if (current_DOR & 0xf0)
		do_floppy_timer();

	// 如果进程运行时间还没完，则退出
	if ((--current->counter) > 0)
		return;
	current->counter = 0;

	// 对于内核态程序，不依赖 counter 值进行调度
	if (!cpl)
		return;
	schedule();
}

// 系统调用功能 - 设置报警定时时间值(秒)
// 如果参数 seconds > 0，则设置该新的定时值并返回原定时值，否则返回 0
int sys_alarm(long seconds)
{
	int old = current->alarm;

	if (old)
		old = (old - jiffies) / HZ;
	current->alarm = (seconds > 0) ? (jiffies + HZ * seconds) : 0;
	return (old);
}

// 取当前进程号 pid
int sys_getpid(void)
{
	return current->pid;
}

// 取父进程号 ppid
int sys_getppid(void)
{
	return current->father;
}

// 取用户号 uid
int sys_getuid(void)
{
	return current->uid;
}

// 取 euid 有效用户 id
int sys_geteuid(void)
{
	return current->euid;
}

// 取组号 gid
int sys_getgid(void)
{
	return current->gid;
}

// 取 egid 有效组
int sys_getegid(void)
{
	return current->egid;
}

// 系统调用功能，降低对 CPU 的使用优先权（有人会用吗？😊）
// 应该限制 increment 大于 0，否则的话，可使优先权增大！！
int sys_nice(long increment)
{
	if (current->priority - increment > 0)
		current->priority -= increment;
	return 0;
}

// 调度程序的初始化子程序
void sched_init(void)
{
	int i;

	// 描述符表结构指针
	struct desc_struct *p;

	// sigaction 是存放有关信号状态的结构
	if (sizeof(struct sigaction) != 16)
		panic("Struct sigaction MUST be 16 bytes");

	// 设置初始任务（任务0）的 任务状态段描述符 和 局部数据表描述符 include/asm/system.h
	set_tss_desc(gdt + FIRST_TSS_ENTRY, &(init_task.task.tss));
	set_ldt_desc(gdt + FIRST_LDT_ENTRY, &(init_task.task.ldt));

	// 清任务数组和描述符表项（注意 i=1 开始，所以初始任务的描述符还在）
	p = gdt + 2 + FIRST_TSS_ENTRY;
	for (i = 1; i < NR_TASKS; i++)
	{
		task[i] = NULL;
		p->a = p->b = 0;
		p++;
		p->a = p->b = 0;
		p++;
	}
	// 清除标志寄存器中的位 NT，这样以后就不会有麻烦
	// NT 标志用于控制程序的递归调用 (Nested Task)
	// 当 NT 置位时当前中断任务执行 iret 指令时就会引起任务切换
	// NT 指出 TSS 中的 back_link 字段是否有效
	__asm__(
		"pushfl \n"
		"andl $0xffffbfff,(%esp) \n"
		"popfl\n");

	// 将任务 0 的 TSS 加载到任务寄存器tr
	ltr(0);

	// 将局部描述符表加载到局部描述符表寄存器
	// 注意！！是将 GDT 中相应 LDT 描述符的选择符加载到 ldtr
	// 只明确加载这一次，以后新任务 LDT 的加载
	// 是 CPU 根据 TSS 中的 LDT 项自动加载。
	lldt(0);

	// 下面代码用于初始化 8253 定时器
	outb_p(0x36, 0x43); /* binary, mode 3, LSB/MSB, ch 0 */

	// 定时值低字节
	outb_p(LATCH & 0xff, 0x40);

	// 定时值高字节
	outb(LATCH >> 8, 0x40);

	// 设置时钟中断处理程序指针（设置时钟中断门）
	// 😔 我不太喜欢句柄这个词，感觉是一种很怪异的指针
	set_intr_gate(0x20, &timer_interrupt);

	// 修改中断控制器屏蔽码，允许时钟中断
	outb(inb_p(0x21) & ~0x01, 0x21);

	// 设置系统调用中断门
	set_system_gate(0x80, &system_call);
}
