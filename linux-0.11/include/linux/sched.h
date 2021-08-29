#ifndef _SCHED_H
#define _SCHED_H

// 系统中同时最多任务（进程）数
#define NR_TASKS 64

// 定义系统时钟滴答频率(1 百赫兹，每个滴答10ms)
#define HZ 100

// 任务 0 比较特殊，所以特意给它单独定义一个符号
#define FIRST_TASK task[0]

// 任务数组中的最后一项任务
#define LAST_TASK task[NR_TASKS - 1]

// head 头文件，定义了段描述符的简单结构，和几个选择符常量
#include <linux/head.h>

// 文件系统头文件，定义文件表结构（file,buffer_head,m_inode 等）
#include <linux/fs.h>

// 内存管理头文件，含有页面大小定义和一些页面释放函数原型
#include <linux/mm.h>

// 信号头文件，定义信号符号常量，信号结构以及信号操作函数原型
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif

// 这里定义了进程运行可能处的状态
#define TASK_RUNNING 0		   // 进程正在运行或已准备就绪
#define TASK_INTERRUPTIBLE 1   // 进程处于可中断等待状态
#define TASK_UNINTERRUPTIBLE 2 // 进程处于不可中断等待状态，主要用于 I/O 操作等待
#define TASK_ZOMBIE 3		   // 进程处于僵死状态，已经停止运行，但父进程还没发信号
#define TASK_STOPPED 4		   // 进程已停止

#ifndef NULL
#define NULL ((void *)0) // 定义 NULL 为空指针
#endif

// 复制进程的页目录页表。Linus 认为这是内核中最复杂的函数之一
extern int copy_page_tables(unsigned long from, unsigned long to, long size);

// 释放页表所指定的内存块及页表本身
extern int free_page_tables(unsigned long from, unsigned long size);

// 调度程序的初始化函数
extern void sched_init(void);

// 进程调度函数
extern void schedule(void);

// 异常(陷阱)中断处理初始化函数，设置中断调用门并允许中断请求信号
extern void trap_init(void);

// 显示内核出错信息，然后进入死循环
#ifndef PANIC
volatile void panic(const char *str);
#endif

// 往 tty 上写指定长度的字符串
extern int tty_write(unsigned minor, char *buf, int count);

// 定义函数指针类型
typedef int (*fn_ptr)();

// 下面是数学协处理器使用的结构，主要用于保存进程切换时 i387 的执行状态信息
struct i387_struct
{
	long cwd;		   // 控制字(Control word)
	long swd;		   // 状态字(Status word)
	long twd;		   // 标记字(Tag word)
	long fip;		   // 协处理器代码指针
	long fcs;		   // 协处理器代码段寄存器
	long foo;		   // 内存操作数的偏移位置
	long fos;		   // 内存操作数的段值
	long st_space[20]; // 8 个 10 字节的协处理器累加器
};

// 任务状态段数据结构
struct tss_struct
{
	long back_link; /* 16 high bits zero */
	long esp0;
	long ss0; /* 16 high bits zero */
	long esp1;
	long ss1; /* 16 high bits zero */
	long esp2;
	long ss2; /* 16 high bits zero */
	long cr3;
	long eip;
	long eflags;
	long eax, ecx, edx, ebx;
	long esp;
	long ebp;
	long esi;
	long edi;
	long es;		   /* 16 high bits zero */
	long cs;		   /* 16 high bits zero */
	long ss;		   /* 16 high bits zero */
	long ds;		   /* 16 high bits zero */
	long fs;		   /* 16 high bits zero */
	long gs;		   /* 16 high bits zero */
	long ldt;		   /* 16 high bits zero */
	long trace_bitmap; /* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};

// 这里是任务（进程）数据结构，或称为进程描述符
struct task_struct
{
	// 这里太难调试了，别动 [我偏偏就是要动了 😜]

	// 任务的运行状态（-1 不可运行，0 可运行(就绪)，>0 已停止）
	long state;

	// 任务运行时间计数(递减)（滴答数），运行时间片
	long counter;

	// 运行优先数。任务开始运行时 counter = priority，越大运行越长
	long priority;

	// 信号：是位图，每个比特位代表一种信号，信号值=位偏移值+1
	long signal;

	// 信号执行属性结构，对应信号将要执行的操作和标志信息
	struct sigaction sigaction[32];

	// 进程信号屏蔽码（对应信号位图）
	long blocked; /* bitmap of masked signals */
				  /* various fields */

	// 任务执行停止的退出码，其父进程会取
	int exit_code;

	// 代码段地址，代码长度（字节数），代码长度 + 数据长度（字节数），总长度（字节数），堆栈段地址
	unsigned long start_code, end_code, end_data, brk, start_stack;

	// 进程标识号(进程号)，父进程号，父进程组号，会话号，会话首领
	long pid, father, pgrp, session, leader;

	// 用户标识号（用户id），有效用户 id，保存的用户id
	unsigned short uid, euid, suid;

	// 组标识号（组id），有效组id，保存的组id
	unsigned short gid, egid, sgid;

	// 报警定时值（滴答数）
	long alarm;

	// 用户态运行时间（滴答数）
	// 系统态运行时间（滴答数）
	// 子进程用户态运行时间
	// 子进程内核态运行时间
	// 进程开始运行时刻
	long utime, stime, cutime, cstime, start_time;

	// 标志：是否使用了协处理器
	unsigned short used_math;
	/* file system info */
	// 进程使用tty 的子设备号。-1 表示没有使
	int tty; /* -1 if no tty, so it must be signed */

	// 文件创建属性屏蔽位
	unsigned short umask;

	// 当前工作目录 i 节点结构
	struct m_inode *pwd;

	// 根目录 i 节点结构
	struct m_inode *root;

	// 执行文件 i 节点结构
	struct m_inode *executable;

	// 执行时关闭文件句柄位图标志
	unsigned long close_on_exec;

	// 进程使用的文件表结构
	struct file *filp[NR_OPEN];

	// 本任务的局部表描述符。0-空，1-代码段cs，2-数据和堆栈段ds&ss
	struct desc_struct ldt[3];

	// 本进程的任务状态段信息结构
	struct tss_struct tss;
};

// INIT_TASK 用于设置第1 个任务表，若想修改，责任自负 😊
// 基址Base = 0，段长limit = 0x9ffff（=640kB）。

// 对应上面任务结构的第1 个任务的信息
#define INIT_TASK                                                \
	{                                                            \
		0,		/* state */                                      \
			15, /* counter */                                    \
			15, /* priority */                                   \
			0,	/* signal  */                                    \
			{                                                    \
				{}, /* sigaction[32] */                          \
			},                                                   \
			0,	  /* blocked */                                  \
			0,	  /* exit_code */                                \
			0,	  /* start_code */                               \
			0,	  /* end_code */                                 \
			0,	  /* end_data */                                 \
			0,	  /* brk */                                      \
			0,	  /* start_stack */                              \
			0,	  /* pid */                                      \
			-1,	  /* father */                                   \
			0,	  /* pgrp */                                     \
			0,	  /* session */                                  \
			0,	  /* leader */                                   \
			0,	  /* uid */                                      \
			0,	  /* euid */                                     \
			0,	  /* suid */                                     \
			0,	  /* gid */                                      \
			0,	  /* egid */                                     \
			0,	  /* sgid */                                     \
			0,	  /* alarm */                                    \
			0,	  /* utime */                                    \
			0,	  /* stime */                                    \
			0,	  /* cutime */                                   \
			0,	  /* cstime */                                   \
			0,	  /* start_time */                               \
			0,	  /* used_math */                                \
			-1,	  /* tty */                                      \
			0022, /* umask */                                    \
			NULL, /* pwd */                                      \
			NULL, /* root */                                     \
			NULL, /* executable */                               \
			0,	  /*  close_on_exec */                           \
			{                                                    \
				NULL, /* filp[NR_OPEN] */                        \
			},                                                   \
			{                                                    \
				/* ldt[3] */                                     \
				{0, 0},                                          \
				{0x9f, 0xc0fa00},                                \
				{0x9f, 0xc0f200},                                \
			},                                                   \
			{                                                    \
				/*tss*/                                          \
				0,							  /* back_link */    \
				PAGE_SIZE + (long)&init_task, /* esp0 */         \
				0x10,						  /* ss0 */          \
				0,							  /* esp1 */         \
				0,							  /* ss1 */          \
				0,							  /* esp2 */         \
				0,							  /* ss2 */          \
				(long)&pg_dir,				  /* cr3 */          \
				0,							  /* eip */          \
				0,							  /* eflags */       \
				0,							  /* eax */          \
				0,							  /* ecx */          \
				0,							  /* edx */          \
				0,							  /* ebx */          \
				0,							  /* esp */          \
				0,							  /* ebp */          \
				0,							  /* esi */          \
				0,							  /* edi */          \
				0x17,						  /* es */           \
				0x17,						  /* cs */           \
				0x17,						  /* ss */           \
				0x17,						  /* ds */           \
				0x17,						  /* fs */           \
				0x17,						  /* gs */           \
				_LDT(0),					  /* ldt */          \
				0x80000000,					  /* trace_bitmap */ \
				{}							  /* i387 */         \
			},                                                   \
	}

// 任务数组
extern struct task_struct *task[NR_TASKS];

// 上一个使用过协处理器的进程
extern struct task_struct *last_task_used_math;

// 当前进程结构指针变量
extern struct task_struct *current;

// 从开机开始算起的滴答数（10ms/滴答）
extern long volatile jiffies;

// 开机时间。从 1970-01-01 00:00:00 开始计时的秒数
extern long startup_time;

// 当前时间（秒数）
#define CURRENT_TIME (startup_time + jiffies / HZ)

// 添加定时器函数（定时时间jiffies 滴答数，定时到时调用函数*fn()）
extern void add_timer(long jiffies, void (*fn)(void));

// 不可中断的等待睡眠
extern void sleep_on(struct task_struct **p);

// 可中断的等待睡眠
extern void interruptible_sleep_on(struct task_struct **p);

// 明确唤醒睡眠的进程
extern void wake_up(struct task_struct **p);

// 寻找第 1 个 TSS 在全局表中的入口
// 0-没有用 nul，
// 1-代码段 cs，
// 2-数据段 ds，
// 3-系统 段syscall
// 4-任务状态段 TSS0
// 5-局部表 LTD0
// 6-任务状态段 TSS1，等

// 全局表中第 1 个任务状态段(TSS)描述符的选择符索引号
#define FIRST_TSS_ENTRY 4

// 全局表中第 1 个局部描述符表(LDT)描述符的选择符索引号
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY + 1)

// 宏定义，计算在全局表中第 n 个任务的 TSS 描述符的索引号（选择符）
#define _TSS(n) ((((unsigned long)n) << 4) + (FIRST_TSS_ENTRY << 3))

// 宏定义，计算在全局表中第 n 个任务的 LDT 描述符的索引号
#define _LDT(n) ((((unsigned long)n) << 4) + (FIRST_LDT_ENTRY << 3))

// 宏定义，加载第 n 个任务的任务寄存器 tr
#define ltr(n) __asm__("ltr %%ax" ::"a"(_TSS(n)))

// 宏定义，加载第 n 个任务的局部描述符表寄存器 ldtr
#define lldt(n) __asm__("lldt %%ax" ::"a"(_LDT(n)))

// 取当前运行任务的任务号（是任务数组中的索引值，与进程号 pid 不同）
// 返回：n - 当前任务号
#define str(n)                  \
	__asm__("str %%ax\n\t"      \
			"subl %2,%%eax\n\t" \
			"shrl $4,%%eax"     \
			: "=a"(n)           \
			: "a"(0), "i"(FIRST_TSS_ENTRY << 3))

// switch_to(n) 将切换当前任务到任务 nr，即 n。首先检测任务 n 不是当前任务，
// 如果是则什么也不做退出
// 如果我们切换到的任务最近（上次运行）使用过数学协处理器的话
// 则还需复位控制寄存器 cr0 中的 TS 标志

// 跳转到一个任务的 TSS 段选择符组成的地址处会造成 CPU 进行任务切换操作
// 输入：
// %0 - 偏移地址(*&__tmp.a)；
// %1 - 存放新TSS 的选择符；
// dx - 新任务 n 的 TSS 段选择符；
// ecx - 新任务指针task[n]
// 其中临时数据结构 __tmp 用于组建远跳转（far jump）指令的操作数
// 该操作数由 4 字节偏移地址和 2 字节的段选择符组成
// 因此 __tmp 中 a 的值是 32 位偏移值
// 而 b 的低 2 字节是新 TSS 段的选择符（高 2 字节不用）
// 跳转到 TSS 段选择符会造成任务切换到该 TSS 对应的进程
// 对于造成任务切换的长跳转，a 值无用
// 长跳转上的内存间接跳转指令使用 6 字节操作数作为跳转目的地的长指针
// 其格式为：jmp 16 位段选择符：32 位偏移值
// 但在内存中操作数的表示顺序与这里正好相反
// 在判断新任务上次执行是否使用过协处理器时
// 是通过将新任务状态段地址与保存在 last_task_used_math 变量中
// 使用过协处理器的任务状态段地址进行比较而作出的
// 参见kernel/sched.c 中函数 math_state_restore()
#define switch_to(n)                                                                                                                                                  \
	{                                                                                                                                                                 \
		struct                                                                                                                                                        \
		{                                                                                                                                                             \
			long a, b;                                                                                                                                                \
		} __tmp;                                                                                                                                                      \
		__asm__("cmpl %%ecx,current\n\t"			 /* 任务n 是当前任务吗?(current ==task[n]?) */                                                            \
				"je 1f\n\t"							 /* 是，则什么都不做，退出 */                                                                          \
				"movw %%dx,%1\n\t"					 /* 将新任务16 位选择符存入__tmp.b 中 */                                                               \
				"xchgl %%ecx,current\n\t"			 /* current = task[n]；ecx = 被切换出的任务 */                                                            \
				"ljmp *%0\n\t"						 /* 执行长跳转至*&__tmp，造成任务切换，在任务切换回来后才会继续执行下面的语句 */ \
				"cmpl %%ecx,last_task_used_math\n\t" /* 新任务上次使用过协处理器吗？ */                                                                 \
				"jne 1f\n\t"						 /* 没有则跳转，退出 */                                                                                   \
				"clts\n"							 /* 新任务上次使用过协处理器，则清 cr0 的 TS 标志 */                                            \
				"1:" ::"m"(*&__tmp.a),				 /*  */                                                                                                           \
				"m"(*&__tmp.b),						 /*  */                                                                                                           \
				"d"(_TSS(n)), "c"((long)task[n]));	 /*  */                                                                                                           \
	}

// 页面地址对齐（在内核代码中没有任何地方引用!!）
#define PAGE_ALIGN(n) (((n) + 0xfff) & 0xfffff000)

// 设置位于地址 addr 处描述符中的各基地址字段(基地址是base)
// %0 - 地址 addr 偏移 2；
// %1 - 地址 addr 偏移 4；
// %2 - 地址 addr 偏移 7；
// edx - 基地址 base；
#define _set_base(addr, base)                 \
	__asm__("push %%edx\n\t"                  \
			"movw %%dx,%0\n\t"                \
			"rorl $16,%%edx\n\t"              \
			"movb %%dl,%1\n\t"                \
			"movb %%dh,%2\n\t"                \
			"pop %%edx" ::"m"(*((addr) + 2)), \
			"m"(*((addr) + 4)),               \
			"m"(*((addr) + 7)),               \
			"d"(base))

// 设置位于地址 addr 处描述符中的段限长字段(段长是 limit)
// %0 - 地址 addr；
// %1 - 地址 addr 偏移 6 处；
// edx - 段长值 limit
#define _set_limit(addr, limit)         \
	__asm__("push %%edx\n\t"            \
			"movw %%dx,%0\n\t"          \
			"rorl $16,%%edx\n\t"        \
			"movb %1,%%dh\n\t"          \
			"andb $0xf0,%%dh\n\t"       \
			"orb %%dh,%%dl\n\t"         \
			"movb %%dl,%1\n\t"          \
			"pop %%edx" ::"m"(*(addr)), \
			"m"(*((addr) + 6)),         \
			"d"(limit))

// 设置局部描述符表中 ldt 描述符的基地址字段
#define set_base(ldt, base) _set_base(((char *)&(ldt)), (base))

// 设置局部描述符表中 ldt 描述符的段长字段
#define set_limit(ldt, limit) _set_limit(((char *)&(ldt)), (limit - 1) >> 12)

// 从地址 addr 处描述符中取段基地址。功能与 _set_base() 正好相反
// edx - 存放基地址(__base)；
// %1 - 地址addr 偏移2；
// %2 - 地址addr 偏移4；
// %3 - addr 偏移7
/**
#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \
	"movb %2,%%dl\n\t" \
	"shll $16,%%edx\n\t" \
	"movw %1,%%dx" \
	:"=d" (__base) \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7)) \
        :"memory"); \
__base;})
**/

static inline unsigned long _get_base(char *addr)
{
	unsigned long __base;
	__asm__("movb %3,%%dh\n\t"
			"movb %2,%%dl\n\t"
			"shll $16,%%edx\n\t"
			"movw %1,%%dx"
			: "=&d"(__base)
			: "m"(*((addr) + 2)),
			  "m"(*((addr) + 4)),
			  "m"(*((addr) + 7)));
	return __base;
}

// 取局部描述符表中 ldt 所指段描述符中的基地址
#define get_base(ldt) _get_base(((char *)&(ldt)))

// 取段选择符 segment 指定的描述符中的段限长值
// 指令 lsl 是 Load Segment Limit 缩写
// 它从指定段描述符中取出分散的限长比特位
// 拼成完整的段限长值放入指定寄存器中
// 所得的段限长是实际字节数减 1
// 因此这里还需要加 1 后才返回
// %0 - 存放段长值(字节数)；
// %1 - 段选择符segment
#define get_limit(segment) (            \
	{                                   \
		unsigned long __limit;          \
		__asm__("lsll %1,%0\n\t incl %0" \
				: "=r"(__limit)         \
				: "r"(segment));        \
		__limit;                        \
	})

#endif
