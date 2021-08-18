/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 定义该变量是为了包括定义在 unistd.h 中的内嵌汇编代码等信息
#define __LIBRARY__
#include <unistd.h>

// 时间类型头文件。其中最主要定义了 tm 结构和一些有关时间的函数原形
#include <time.h>

// 我们需要下面这些内嵌语句
// 从内核空间创建进程 (forking) 将导致没有写时复制（COPY ON WRITE）!!!
// 直到执行一个 execve 调用。这对堆栈可能带来问题
// 处理的方法是在 fork() 调用之后不让 main() 使用任何堆栈
// 因此就不能有函数调用 - 这意味着 fork 也要使用内嵌的代码
// 否则我们在从 fork() 退出时就要使用堆栈了
// 实际上只有 pause 和 fork 需要使用内嵌方式，以保证从 main() 中不会弄乱堆栈，
// 但是我们同时还定义了其它一些函数

// 本程序将会在移动到用户模式（切换到任务0）后才执行 fork()
// 因此避免了在内核空间写时复制问题
// 在执行了 moveto_user_mode() 之后，本程序就以任务0 的身份在运行了
// 而任务 0 是所有将创建的子进程的父进程
// 当创建第一个子进程时，任务 0 的堆栈也会被复制。
// 因此希望在 main.c 运行在任务 0 的环境下时不要有对堆栈的任何操作
// 以免弄乱堆栈，从而也不会弄乱所有子进程的堆栈

/*
always_inline 属性是后加的，该属性有以下功能：

- 忽略 -fno-inline 选项
- 忽略 inline-limit，意味着无论扩展出来的代码多大都可以。
- 不会生成外部定义，用于外部链接
- 忽略所有优化选项

*/

static inline fork(void) __attribute__((always_inline));
static inline pause(void) __attribute__((always_inline));

// 这是 unistd.h 中的内嵌宏代码。以嵌入汇编的形式调用 Linux 的系统调用中断 0x80
// 该中断是所有系统调用的入口，该条语句实际上是 int fork() 创建进程系统调用
// syscall0 名称中最后的 0 表示无参数，1 表示 1 个参数，等等
static inline _syscall0(int, fork)

	// int pause() 系统调用：暂停进程的执行，直到收到一个信号
	static inline _syscall0(int, pause)

	// int setup(void * BIOS)系统调用，仅用于 linux 初始化（仅在这个程序中被调用）
	static inline _syscall1(int, setup, void *, BIOS)

	// int sync() 系统调用：更新文件系统
	static inline _syscall0(int, sync)

// tty 头文件，定义了有关tty_io，串行通信方面的参数、常数
#include <linux/tty.h>

// 调度程序头文件，定义了任务结构 task_struct、第 1 个初始任务的数据
// 还有一些以宏的形式定义的有关描述符参数设置和获取的内联汇编函数程序
#include <linux/sched.h>

// head 头文件，定义了段描述符的简单结构，和几个选择符常量
#include <linux/head.h>

// 系统头文件。以宏的形式定义了许多有关
// 设置或修改 描述符/中断门 等的内联汇编子程序
#include <asm/system.h>

// io 头文件，以宏的内联编程序形式定义对 io 端口操作的函数
#include <asm/io.h>

// 标准定义头文件，定义了NULL, offsetof(TYPE, MEMBER)
#include <stddef.h>

// 标准参数头文件。以宏的形式定义变量参数列表。
// 主要说明了一个类型 (va_list) 和三个宏 (va_start, va_arg 和 va_end)，
// vsprintf、vprintf、vfprintf
#include <stdarg.h>

#include <unistd.h>

// 文件控制头文件，用于文件及其描述符的操作控制常数符号的定义
#include <fcntl.h>

// 类型头文件，定义了基本的系统数据类型
#include <sys/types.h>

// 文件系统头文件，定义文件表结构（file,buffer_head,m_inode 等）
#include <linux/fs.h>

	// 静态字符串数组，用作内核显示信息的缓存
	static char printbuf[1024];

// 格式化输出到一字符串中
extern int vsprintf();

// 初始化函数原形
extern void init(void);

// 块设备初始化子程序
extern void blk_dev_init(void);

// 字符设备初始化
extern void chr_dev_init(void);

// 硬盘初始化程序
extern void hd_init(void);

// 软驱初始化程序
extern void floppy_init(void);

// 内存管理初始化
extern void mem_init(long start, long end);

// 虚拟盘初始化
extern long rd_init(long mem_start, int length);

// 计算系统开机启动时间（秒）
extern long kernel_mktime(struct tm *tm);

// 内核启动时间（开机时间）（秒）
extern long startup_time;

// 以下这些数据是由 setup.s 程序在引导时间设置的

// 1M 以后的扩展内存大小（KB）
#define EXT_MEM_K (*(unsigned short *)0x90002)

// 硬盘参数表基址
#define DRIVE_INFO (*(struct drive_info *)0x90080)

// 根文件系统所在设备号
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

// 是啊，是啊，下面这段程序很差劲，但我不知道如何正确地实现，而且好象它还能运行。
// 如果有关于实时时钟更多的资料，那我很感兴趣
// 这些都是试探出来的，以及看了一些 bios 程序，唉 😔！

// 这段宏读取CMOS 实时时钟信息
// 0x70 是写端口号，0x80|addr 是要读取的 CMOS 内存地址
// 0x71 是读端口号
#define CMOS_READ(addr) (          \
	{                              \
		outb_p(0x80 | addr, 0x70); \
		inb_p(0x71);               \
	})

// 定义宏 将 BCD 码转换成二进制数值
#define BCD_TO_BIN(val) ((val) = ((val)&15) + ((val) >> 4) * 10)

// 该函数取 CMOS 时钟，并设置开机时间到 startup_time (秒)
static void time_init(void)
{
	struct tm time;

	// CMOS 的访问速度很慢，为了减小时间误差
	// 在读取了下面循环中所有数值后，若此时CMOS 中秒值发生了变化
	// 那么就重新读取所有值，这样内核就能把与 CMOS 的时间误差控制在 1 秒以内了

	do
	{
		time.tm_sec = CMOS_READ(0);	 // 当前时间秒值（均是BCD 码值）
		time.tm_min = CMOS_READ(2);	 // 当前分钟值
		time.tm_hour = CMOS_READ(4); // 当前小时值
		time.tm_mday = CMOS_READ(7); // 一月中的当天日期
		time.tm_mon = CMOS_READ(8);	 // 当前月份（1—12）
		time.tm_year = CMOS_READ(9); // 当前年份。
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec); // 转换成二进制数值
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--; // tm_mon 中月份范围是 0 ~ 11

	// 计算从1970 年 1 月 1 日 0 时起到开机时经过的秒数，作为开机时间
	startup_time = kernel_mktime(&time);
}

// 机器具有的物理内存容量（字节数）
static long memory_end = 0;

// 高速缓冲区末端地址
static long buffer_memory_end = 0;

// 主内存（将用于分页）开始的位置
static long main_memory_start = 0;

// 用于存放硬盘参数表信息
struct drive_info
{
	char dummy[32];
} drive_info;

/* 这里确实是 void，并没错。在 startup 程序 (head.s) 中就是这样假设的 */
void main(void)
{
	// 此时中断仍被禁止着，做完必要的设置后就将其开启

	// 下面这段代码用于保存：

	// 根设备号 -> ROOT_DEV；
	// 高速缓存末端地址 -> buffer_memory_end；
	// 机器内存数 -> memory_end；
	// 主内存开始地址 -> main_memory_start；

	ROOT_DEV = ORIG_ROOT_DEV;

	// 复制0x90080 处的硬盘参数表。
	drive_info = DRIVE_INFO;

	// 内存大小=1Mb 字节+扩展内存(k)*1024 字节
	memory_end = (1 << 20) + (EXT_MEM_K << 10);

	// 忽略不到4Kb（1 页）的内存数。
	memory_end &= 0xfffff000;

	// 如果内存超过 16Mb，则按 16Mb 计
	if (memory_end > 16 * 1024 * 1024)
		memory_end = 16 * 1024 * 1024;

	if (memory_end > 12 * 1024 * 1024)
		buffer_memory_end = 4 * 1024 * 1024; // 如果内存 > 12Mb，则设置缓冲区末端 = 4Mb
	else if (memory_end > 6 * 1024 * 1024)
		buffer_memory_end = 2 * 1024 * 1024; // 否则如果内存>6Mb，则设置缓冲区末端=2Mb
	else
		buffer_memory_end = 1 * 1024 * 1024; // 否则则设置缓冲区末端=1Mb

	// 主内存起始位置=缓冲区末端
	main_memory_start = buffer_memory_end;

#ifdef RAMDISK
	// 如果定义了内存虚拟盘，则初始化虚拟盘。此时主内存将减少
	main_memory_start += rd_init(main_memory_start, RAMDISK * 1024);
#endif

	// 以下是内核进行所有方面的初始化工作。
	// 阅读时最好跟着调用的程序深入进去看，若实在看不下去了
	// 就先放一放，继续看下一个初始化调用，这是经验之谈 😜。

	mem_init(main_memory_start, memory_end);

	// 陷阱门（硬件中断向量）初始化
	trap_init();

	// 块设备初始化
	blk_dev_init();

	// 字符设备初始化
	chr_dev_init();

	// tty 初始化
	tty_init();

	// 设置开机启动时间 -> startup_time
	time_init();

	// 调度程序初始化
	sched_init();

	// 缓冲管理初始化，建内存链表等
	buffer_init(buffer_memory_end);

	// 硬盘初始化
	hd_init();

	// 软驱初始化
	floppy_init();

	// 所有初始化工作都做完了，开启中断
	sti();

	// 下面过程通过在堆栈中设置的参数，利用中断返回指令启动任务 0 执行
	// 移到用户模式下执行
	move_to_user_mode();

	if (!fork())
	{
		// 在新建的子进程（任务1）中执行
		init();
	}

	// 下面代码开始以任务 0 的身份运行

	//  注意!! 对于任何其它的任务，'pause()' 将意味着我们必须等待收到一个信号才会返回就绪运行态
	// 但任务 0（task0）是唯一的例外情况（参见'schedule()'），
	// 因为任务 0 在任何空闲时间里都会被激活（当没有其它任务在运行时）
	// 因此对于任务 0 'pause()' 仅意味着我们返回来查看是否有其它任务可以运行
	// 如果没有的话我们就回到这里，一直循环执行 'pause()'。

	// pause()系统调用会把任务 0 转换成可中断等待状态，再执行调度函数。
	// 但是调度函数只要发现系统中没有其它任务可以运行时就会切换到任务 0，
	// 而不依赖于任务 0 的状态。

	// 简单说，任务 0 就是空闲进程，用于占位，这样的话，所有进程都阻塞之后，空闲进程可以一直执行。
	for (;;)
		pause();
}

static int printf(const char *fmt, ...)
{
	// 产生格式化信息并输出到标准输出设备 stdout
	va_list args;
	int i;

	va_start(args, fmt);
	write(1, printbuf, i = vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

// 调用执行程序时参数的字符串数组
static char *argv_rc[] = {"/bin/sh", NULL};

// 调用执行程序时的环境字符串数组
static char *envp_rc[] = {"HOME=/", NULL};

// 同上
static char *argv[] = {"-/bin/sh", NULL};
static char *envp[] = {"HOME=/usr/root", NULL};
// 上面 argv[0] 中的字符 “-” 是传递给 shell 程序 sh 的一个标志
// 通过识别该标志，sh 程序会作为登录 shell 执行
// 其执行过程与在 shell 提示符下执行 sh 不太一样

// 在 main() 中已经进行了系统初始化，包括内存管理、各种硬件设备和驱动程序。
// init() 函数运行在任务 0 第 1 次创建的子进程（任务1）中。
// 它首先对第一个将要执行的程序（shell）的环境进行初始化，然后加载该程序并执行之
void init(void)
{
	int pid, i;

	// 这是一个系统调用
	// 用于读取硬盘参数包括分区表信息并加载虚拟盘（若存在的话）和安装根文件系统设备
	// 该函数是用宏定义的，对应函数是sys_setup()
	setup((void *)&drive_info);

	// 下面以读写访问方式打开设备 “/dev/tty0”，它对应终端控制台
	// 由于这是第一次打开文件操作，因此产生的文件句柄号（文件描述符）肯定是 0
	// 该句柄是 UNIX 类操作系统默认的控制台标准输入句柄 stdin
	// 这里把它以读和写的方式打开是为了复制产生标准输出（写）句柄 stdout
	// 和标准错误输出句柄 stderr

	// 打开 stdin
	(void)open("/dev/tty0", O_RDWR, 0);

	// 复制句柄，产生 1 号句柄 -- stdout 标准输出设备
	(void)dup(0);

	// 复制句柄，产生 2 号句柄 -- stderr 标准错误输出设备
	(void)dup(0);

	// 下面打印缓冲区块数和总字节数，每块 1024 字节
	printf("%d buffers = %d bytes buffer space\n\r", NR_BUFFERS, NR_BUFFERS * BLOCK_SIZE);

	// 主内存区空闲内存字节数
	printf("Free mem: %d bytes\n\r", memory_end - main_memory_start);

	// 下面 fork() 用于创建一个子进程(任务2)。
	// 对于被创建的子进程，fork() 将返回 0 值
	// 对于原进程 (父进程) 则返回子进程的进程号pid
	// 所以 if (!(pid = fork())) 内是子进程执行的内容，
	// 该子进程关闭了句柄 0(stdin)、以只读方式打开 /etc/rc 文件，
	// 并使用 execve() 调用将进程自身替换成 /bin/sh 程序 (即shell 程序)
	// 然后执行 /bin/sh 程序，所带参数和环境变量分别由 argv_rc 和envp_rc 数组给出
	// 函数 _exit() 退出时的出错码
	// 1 – 操作未许可
	// 2 -- 文件或目录不存在

	if (!(pid = fork()))
	{
		// 关闭了(stdin)
		close(0);

		// 如果打开文件失败，则退出
		if (open("/etc/rc", O_RDONLY, 0))
			_exit(1);

		// 当前进程替换成 /bin/sh 程序并执行
		execve("/bin/sh", argv_rc, envp_rc);

		// 若 execve() 执行失败则退出
		_exit(2);
	}
	// 下面还是父进程（init）执行的语句
	// wait() 等待子进程停止或终止，返回值应是子进程的进程号(pid)
	// 这三句的作用是父进程等待子进程的结束
	// &i 是存放返回状态信息的位置
	// 如果 wait() 返回值不等于子进程号，则继续等待

	if (pid > 0)
		while (pid != wait(&i))
			/* nothing */;

	// 如果执行到这里，说明刚创建的子进程的执行已停止或终止了
	// 下面循环中首先再创建一个子进程
	// 如果出错，则显示 “初始化程序创建子进程失败” 信息并继续执行
	// 对于所创建的子进程将关闭所有以前还遗留的文件 (stdin, stdout, stderr)
	// 新创建一个会话并设置进程组号，然后重新打开 /dev/tty0 作为stdin，
	// 并复制成 stdout 和 stderr。再次执行系统解释程序 /bin/sh
	// 但这次执行所选用的参数和环境数组另选了一套
	// 然后父进程再次运行 wait() 等待。
	// 如果子进程又停止了执行，则在标准输出上显示出错信息“子进程 pid 停止了运行，返回码是 i”，
	// 然后继续重试下去…，形成“大”死循环。
	while (1)
	{
		if ((pid = fork()) < 0)
		{
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) // 新的子进程
		{
			close(0);
			close(1);
			close(2);
			setsid(); // 创建一新的会话期，见文档说明
			(void)open("/dev/tty0", O_RDWR, 0);
			(void)dup(0);
			(void)dup(0);
			_exit(execve("/bin/sh", argv, envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r", pid, i);
		sync(); // 同步操作，刷新缓冲区
	}
	_exit(0); /* 注意！是_exit()，不是exit() */

	// _exit() 和 exit() 都用于正常终止一个函数
	// 但 _exit() 直接是一个 sys_exit 系统调用
	// 而 exit() 则通常是普通函数库中的一个函数
	// 它会先执行一些清除操作，例如调用执行各终止处理程序
	// 关闭所有标准 IO 等，然后调用sys_exit
}
