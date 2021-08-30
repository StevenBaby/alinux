/*
 *  linux/kernel/floppy.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 02.12.91 - 修改成静态变量，以适应复位和重新校正操作
// 这使得某些事情做起来较为方便（output_byte 复位检查等）
// 并且意味着在出错时中断跳转要少一些，所以也希望代码能更容易被理解

// 这个文件当然比较混乱，我已经尽我所能使其能够工作
// 但我不喜欢软驱编程，而且我也只有一个软驱
// 另外，我应该做更多的查错工作，以及改正更多的错误
// 对于某些软盘驱动器，本程序好象还存在一些问题
// 我已经尝试着进行纠正了，但不能保证问题已消失

// 如同 hd.c 文件一样，该文件中的所有子程序都能够被中断调用。所以需要特别地小心
// 硬件中断处理程序是不能睡眠的，否则内核就会傻掉(死机)😊
// 因此不能直接调用 floppy-on，而只能设置一个特殊的定时中断等

// 另外，我不能保证该程序能在多于 1 个软驱的系统上工作，有可能存在错误

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

// 软驱的主设备号是 2
#define MAJOR_NR 2
#include "blk.h"

// 标志：需要重新校正
static int recalibrate = 0;

// 标志：需要进行复位操作
static int reset = 0;

// 标志：需要执行寻道
static int seek = 0;

// 当前数字输出寄存器(Digital Output Register)，定义在sched.c
// 该变量是软驱操作中的重要标志变量
extern unsigned char current_DOR;

// 字节直接输出
#define immoutb_p(val, port) \
	__asm__("outb %0,%1\n\t jmp 1f\n 1:\t jmp 1f\n 1:" ::"a"((char)(val)), "i"(port))

// 这两个定义用于计算软驱的设备号，次设备号 = TYPE*4 + DRIVE

// 软驱类型（2--1.2Mb，7--1.44Mb）
#define TYPE(x) ((x) >> 2)

// 软驱序号（0--3 对应 A--D）
#define DRIVE(x) ((x)&0x03)
/*
 * Note that MAX_ERRORS=8 doesn't imply that we retry every bad read
 * max 8 times - some types of errors increase the errorcount by 2,
 * so we might actually retry only 5-6 times before giving up.
 */

// 注意，下面定义 MAX_ERRORS=8 并不表示对每次读错误尝试最多 8 次
// 有些类型的错误将把出错计数值乘 2，所以我们实际上在放弃操作之前只需尝试 5-6 遍即可
#define MAX_ERRORS 8

// 下面是函数 result() 使用的全局变量

// FDC 最多返回 7 字节的结果信息
#define MAX_REPLIES 7

// 存放 FDC 返回的应答结果信息
static unsigned char reply_buffer[MAX_REPLIES];

// 返回结果状态字节 0
#define ST0 (reply_buffer[0])

// 返回结果状态字节 1
#define ST1 (reply_buffer[1])

// 返回结果状态字节 2
#define ST2 (reply_buffer[2])

// 返回结果状态字节 3
#define ST3 (reply_buffer[3])

// 下面的软盘结构定义了不同的软盘类型
// 与 minix 不同的是，linux 没有 搜索正确的类型 类型
// 因为对其处理的代码令人费解且怪怪的，本程序已经让我遇到了许多的问题了

// 对某些类型的软盘（例如在1.2MB 驱动器中的360kB 软盘等）
// 'stretch'用于检测磁道是否需要特殊处理，其它参数应该是自明的

// 软盘参数有：
// size 大小(扇区数)；
// sect 每磁道扇区数；
// head 磁头数；
// track 磁道数；
// stretch 对磁道是否要特殊处理（标志）；
// gap 扇区间隙长度(字节数)；
// rate 数据传输速率；
// spec1 参数（高 4 位步进速率，低 4 位磁头卸载时间）
static struct floppy_struct
{
	unsigned int size, sect, head, track, stretch;
	unsigned char gap, rate, spec1;
} floppy_type[] = {
	{0, 0, 0, 0, 0, 0x00, 0x00, 0x00},		/* no testing */
	{720, 9, 2, 40, 0, 0x2A, 0x02, 0xDF},	/* 360kB PC diskettes */
	{2400, 15, 2, 80, 0, 0x1B, 0x00, 0xDF}, /* 1.2 MB AT-diskettes */
	{720, 9, 2, 40, 1, 0x2A, 0x02, 0xDF},	/* 360kB in 720kB drive */
	{1440, 9, 2, 80, 0, 0x2A, 0x02, 0xDF},	/* 3.5" 720kB diskette */
	{720, 9, 2, 40, 1, 0x23, 0x01, 0xDF},	/* 360kB in 1.2MB drive */
	{1440, 9, 2, 80, 0, 0x23, 0x01, 0xDF},	/* 720kB in 1.2MB drive */
	{2880, 18, 2, 80, 0, 0x1B, 0x00, 0xCF}, /* 1.44MB diskette */
};

// 上面速率 rate：0 表示 500kb/s，1 表示 300kbps，2 表示 250kbps
// 参数 spec1 是 0xSH，其中 S 是步进速率（F-1 毫秒，E-2ms，D=3ms 等）
// H 是磁头卸载时间（1=16ms，2=32ms 等）

// spec2 是（HLD<<1 | ND），其中HLD 是磁头加载时间（1=2ms，2=4ms 等）
// ND 置位表示不使用DMA（No DMA），在程序中硬编码成6（HLD=6ms，使用DMA）

// system_call.s 中软驱中断过程标号
extern void floppy_interrupt(void);

// boot/head.s 定义的软盘缓冲区
extern char tmp_floppy_area[1024];

// 下面是一些全局变量，因为这是将信息传给中断程序最简单的方式
// 它们是用于当前请求项的数据
static int cur_spec1 = -1;
static int cur_rate = -1;
static struct floppy_struct *floppy = floppy_type;
static unsigned char current_drive = 0;
static unsigned char sector = 0;
static unsigned char head = 0;
static unsigned char track = 0;
static unsigned char seek_track = 0;

// 当前磁头所在磁道号
static unsigned char current_track = 255;
static unsigned char command = 0;

// 软驱已选定标志，在处理一个软驱的请求项之前需要首先选定一个软驱
unsigned char selected = 0;
struct task_struct *wait_on_floppy_select = NULL;

// 取消选定软驱，复位软驱已选定标志 selected
// 数字输出寄存器(DOR)的低 2 位用于指定选择的软驱（0-3 对应 A-D）
void floppy_deselect(unsigned int nr)
{
	if (nr != (current_DOR & 3))
		printk("floppy_deselect: drive not selected\n\r");
	selected = 0;
	wake_up(&wait_on_floppy_select);
}

// floppy-change() 不是从中断程序中调用的，所以这里我们可以轻松一下，睡眠等
// 注意 floppy-on() 会尝试设置 current_DOR 指向所需的驱动器
// 但当同时使用几个软盘时不能睡眠：因此此时只能使用循环方式

// 检测指定软驱中软盘更换情况；如果软盘更换了则返回 1，否则返回 0
int floppy_change(unsigned int nr)
{
repeat:
	// 启动指定软驱 nr
	floppy_on(nr);

	// 如果当前选择的软驱不是指定的软驱 nr，并且已经选定了其它软驱
	// 则让当前任务进入可中断等待状态
	while ((current_DOR & 3) != nr && selected)
		interruptible_sleep_on(&wait_on_floppy_select);

	// 如果当前没有选择其它软驱或者当前任务被唤醒时
	// 当前软驱仍然不是指定的软驱 nr，则循环等待
	if ((current_DOR & 3) != nr)
		goto repeat;

	// 取数字输入寄存器值，如果最高位（位7）置位
	// 则表示软盘已更换，此时关闭马达并退出返回 1
	// 否则关闭马达退出返回 0
	if (inb(FD_DIR) & 0x80)
	{
		floppy_off(nr);
		return 1;
	}
	floppy_off(nr);
	return 0;
}

// 复制内存缓冲块，共 1024 字节
#define copy_buffer(from, to) \
	__asm__("cld \n rep \n movsl" ::"c"(BLOCK_SIZE / 4), "S"((long)(from)), "D"((long)(to)))

// 设置（初始化）软盘 DMA 通道
static void setup_DMA(void)
{
	// 当前请求项缓冲区所处内存中位置（地址）
	long addr = (long)CURRENT->buffer;

	cli();

	// 如果缓冲区处于内存 1M 以上的地方，则将 DMA 缓冲区设在临时缓冲区域(tmp_floppy_area)处
	// 因为 8237A 芯片只能在 1M 地址范围内寻址，如果是写盘命令，则需要把数据复制到该临时区域
	if (addr >= 0x100000)
	{
		addr = (long)tmp_floppy_area;
		if (command == FD_WRITE)
			copy_buffer(CURRENT->buffer, tmp_floppy_area);
	}
	// 屏蔽 DMA 通道 2
	// 单通道屏蔽寄存器端口为 0x10。位0-1 指定 DMA 通道(0--3)，位2：1 表示屏蔽，0 表示允许请求
	immoutb_p(4 | 2, 10);
	/* output command byte. I don't know why, but everyone (minix, */
	/* sanches & canton) output this twice, first to 12 then to 11 */

	// 输出命令字节。我是不知道为什么，但是每个人（minix，sanches 和 canton）都输出两次
	// 首先是 12 口，然后是 11 口
	// 下面内联汇编代码向 DMA 控制器端口 12 和 11 写方式字（读盘0x46，写盘0x4A）
	__asm__("outb %%al,$12\n\tjmp 1f\n1:\tjmp 1f\n1:\t"
			"outb %%al,$11\n\tjmp 1f\n1:\tjmp 1f\n1:" ::
				"a"((char)((command == FD_READ) ? DMA_READ : DMA_WRITE)));

	// 地址低 0-7 位
	// 向 DMA 通道 2 写入基/当前地址寄存器（端口4）
	immoutb_p(addr, 4);
	addr >>= 8;

	// 地址高 8-15 位
	immoutb_p(addr, 4);
	addr >>= 8;

	// 地址 16-19 位
	// DMA 只可以在 1M 内存空间内寻址，其高 16-19 位地址需放入页面寄存器(端口0x81)
	immoutb_p(addr, 0x81);

	// 计数器低 8 位(1024-1 = 0x3ff)
	// 向 DMA 通道 2 写入基/当前字节计数器值（端口5）
	immoutb_p(0xff, 5);

	// 计数器高 8 位
	// 一次共传输 1024 字节（两个扇区）
	immoutb_p(3, 5);

	// 开启 DMA 通道 2 的请求
	// 复位对 DMA 通道 2 的屏蔽，开放 DMA2 请求 DREQ 信号
	immoutb_p(0 | 2, 10);
	sti();
}

// 向软驱控制器输出一个字节（命令或参数）
// 在向控制器发送一个字节之前，控制器需要处于准备好状态
// 并且数据传输方向是CPU -> FDC，
// 因此需要首先读取控制器状态信息
// 这里使用了循环查询方式，以作适当延时
static void output_byte(char byte)
{
	int counter;
	unsigned char status;

	if (reset)
		return;

	// 循环读取主状态控制器 FD_STATUS(0x3f4) 的状态
	// 如果状态是 STATUS_READY 并且 STATUS_DIR=0 (CPU -> FDC)
	// 则向数据端口输出指定字节
	for (counter = 0; counter < 10000; counter++)
	{
		status = inb_p(FD_STATUS) & (STATUS_READY | STATUS_DIR);
		if (status == STATUS_READY)
		{
			outb(byte, FD_DATA);
			return;
		}
	}
	// 如果到循环 1 万次结束还不能发送，则置复位标志，并打印出错信息
	reset = 1;
	printk("Unable to send byte to FDC\n\r");
}

// 读取 FDC 执行的结果信息
// 结果信息最多 7 个字节，存放在 reply_buffer[] 中
// 返回读入的结果字节数，若返回值 = -1 表示出错，程序处理方式与上面函数类似
static int result(void)
{
	int i = 0, counter, status;

	// 若复位标志已置位，则立刻退出
	if (reset)
		return -1;
	for (counter = 0; counter < 10000; counter++)
	{
		status = inb_p(FD_STATUS) & (STATUS_DIR | STATUS_READY | STATUS_BUSY);

		// 如果控制器状态是 READY，表示已经没有数据可取，返回已读取的字节数
		if (status == STATUS_READY)
			return i;

		// 如果控制器状态是方向标志置位（CPU -> FDC），已准备好、忙，表示有数据可读取
		// 于是把控制器中的结果数据读入到应答结果数组中，最多读取 MAX_REPLIES（7）个字节
		if (status == (STATUS_DIR | STATUS_READY | STATUS_BUSY))
		{
			if (i >= MAX_REPLIES)
				break;
			reply_buffer[i++] = inb_p(FD_DATA);
		}
	}
	reset = 1;
	printk("Getstatus times out\n\r");
	return -1;
}

// 软盘操作出错中断调用函数，由软驱中断处理程序调用
static void bad_flp_intr(void)
{
	// 当前请求项出错次数增 1
	CURRENT->errors++;

	// 如果当前请求项出错次数大于最大允许出错次数，则取消选定当前软驱
	// 并结束该请求项（缓冲区内容没有被更新）
	if (CURRENT->errors > MAX_ERRORS)
	{
		floppy_deselect(current_drive);
		end_request(0);
	}

	// 如果当前请求项出错次数大于最大允许出错次数的一半，则置复位标志
	// 需对软驱进行复位操作，然后再试；否则软驱需重新校正一下，再试
	if (CURRENT->errors > MAX_ERRORS / 2)
		reset = 1;
	else
		recalibrate = 1;
}

// OK，下面的中断处理函数是在 DMA 读/写成功后调用的
// 这样我们就可以检查执行结果，并复制缓冲区中的数据

// 软盘读写操作中断调用函数
// 在软驱控制器操作结束后引发的中断处理过程中被调用（Bottom half）
static void rw_interrupt(void)
{

	// 读取FDC 执行的结果信息
	// 如果返回结果字节数不等于 7，或者状态字节 0、1 或 2 中存在出错标志
	// 那么，若是写保护就显示出错信息，释放当前驱动器，并结束当前请求项
	// 否则就执行出错计数处理，然后继续执行软盘请求项操作
	// 以下状态的含义参见 fdreg.h 文件
	// ( 0xf8 = ST0_INTR | ST0_SE | ST0_ECE | ST0_NR )
	// ( 0xbf = ST1_EOC | ST1_CRC | ST1_OR | ST1_ND | ST1_WP | ST1_MAM，应该是 0xb7)
	// ( 0x73 = ST2_CM | ST2_CRC | ST2_WC | ST2_BC | ST2_MAM )
	if (result() != 7 || (ST0 & 0xf8) || (ST1 & 0xbf) || (ST2 & 0x73))
	{
		if (ST1 & 0x02)
		{
			printk("Drive %d is write protected\n\r", current_drive);
			floppy_deselect(current_drive);
			end_request(0);
		}
		else
			bad_flp_intr();
		do_fd_request();
		return;
	}

	// 如果当前请求项的缓冲区位于 1M 地址以上，则说明此次软盘读操作的内容还放在临时缓冲区内
	// 需要复制到当前请求项的缓冲区中（因为 DMA 只能在 1M 地址范围寻址）
	if (command == FD_READ && (unsigned long)(CURRENT->buffer) >= 0x100000)
		copy_buffer(tmp_floppy_area, CURRENT->buffer);

	// 释放当前软驱（放弃不选定），执行当前请求项结束处理：
	// 唤醒等待该请求项的进行，唤醒等待空闲请求项的进程（若有的话）
	// 从软驱设备请求项链表中删除本请求项，再继续执行其它软盘请求项操作
	floppy_deselect(current_drive);
	end_request(1);
	do_fd_request();
}

// 设置 DMA 并输出软盘操作命令和参数（输出 1 字节命令 + 0~7 字节参数）
static inline void setup_rw_floppy(void)
{
	// 初始化软盘 DMA 通道
	setup_DMA();

	// 置软盘中断调用函数指针
	do_floppy = rw_interrupt;

	// 发送命令字节
	output_byte(command);

	// 参数（磁头号+驱动器号）
	output_byte(head << 2 | current_drive);

	// 参数（磁道号）
	output_byte(track);

	// 参数（磁头号）
	output_byte(head);

	// 参数（起始扇区号）
	output_byte(sector);

	// 参数(字节数(N=2)512 字节)
	output_byte(2);

	// 参数（每磁道扇区数）
	output_byte(floppy->sect);

	// 参数（扇区间隔长度）
	output_byte(floppy->gap);

	// 参数（当N=0 时，扇区定义的字节长度），这里无用
	output_byte(0xFF); /* sector size (0xff when n!=0 ?) */

	// 若复位标志已置位，则继续执行下一软盘操作请求
	if (reset)
		do_fd_request();
}

// 该子程序是在每次软盘控制器寻道（或重新校正）中断后被调用的
// 注意 "unexpected interrupt"(意外中断) 子程序也会执行重新校正操作，但不在此地

// 寻道处理结束后中断过程中调用的函数
// 首先发送检测中断状态命令，获得状态信息 ST0 和磁头所在磁道信息
// 若出错则执行错误计数检测处理或取消本次软盘操作请求项
// 否则根据状态信息设置当前磁道变量，然后调用函数 setup_rw_floppy() 设置 DMA
// 并输出软盘读写命令和参数
static void seek_interrupt(void)
{
	// 检测驱动器状态

	// 发送检测中断状态命令，该命令不带参数
	// 返回结果信息是两个字节：ST0 和磁头当前磁道号
	output_byte(FD_SENSEI);

	// 读取 FDC 执行的结果信息，如果返回结果字节数不等于2
	// 或者 ST0 不为寻道结束，或者磁头所在磁道(ST1) 不等于设定磁道
	// 则说明发生了错误，于是执行检测错误计数处理，
	// 然后继续执行软盘请求项，并退出
	if (result() != 2 || (ST0 & 0xF8) != 0x20 || ST1 != seek_track)
	{
		bad_flp_intr();
		do_fd_request();
		return;
	}

	// 设置当前磁道
	current_track = ST1;

	// 设置 DMA 并输出软盘操作命令和参数
	setup_rw_floppy();
}

// 该函数是在传输操作的所有信息都正确设置好后被调用的
// 也即软驱马达已开启并且已选择了正确的软盘（软驱）

// 读写数据传输函数
static void transfer(void)
{
	// 首先看当前驱动器参数是否就是指定驱动器的参数
	// 若不是就发送设置驱动器参数命令及相应参数
	// 参数1：高 4 位步进速率，低四位磁头卸载时间；
	// 参数2：磁头加载时间
	if (cur_spec1 != floppy->spec1)
	{
		cur_spec1 = floppy->spec1;
		output_byte(FD_SPECIFY); // 发送设置磁盘参数命令
		output_byte(cur_spec1);	 /* hut etc */
		output_byte(6);			 /* Head load time =6ms, DMA */
	}

	// 判断当前数据传输速率是否与指定驱动器的一致
	// 若不是就发送指定软驱的速率值到数据传输速率控制寄存器(FD_DCR)
	if (cur_rate != floppy->rate)
		outb_p(cur_rate = floppy->rate, FD_DCR);

	// 若返回结果信息表明出错，则再调用软盘请求函数，并返回
	if (reset)
	{
		do_fd_request();
		return;
	}

	// 若寻道标志为零（不需要寻道）
	// 则设置 DMA 并发送相应读写操作命令和参数，然后返回
	if (!seek)
	{
		setup_rw_floppy();
		return;
	}

	// 否则执行寻道处理，置软盘中断处理调用函数为寻道中断函数
	do_floppy = seek_interrupt;

	// 如果起始磁道号不等于零则发送磁头寻道命令和参数
	if (seek_track)
	{
		// 发送磁头寻道命令
		output_byte(FD_SEEK);

		// 发送参数：磁头号+当前软驱号
		output_byte(head << 2 | current_drive);

		// 发送参数：磁道号
		output_byte(seek_track);
	}
	else
	{
		// 发送重新校正命令（磁头归零）
		output_byte(FD_RECALIBRATE);

		// 发送参数：磁头号+当前软驱号
		output_byte(head << 2 | current_drive);
	}

	// 如果复位标志已置位，则继续执行软盘请求项
	if (reset)
		do_fd_request();
}

// 特殊情况 - 用于意外中断（或复位）处理后

// 软驱重新校正中断调用函数
// 首先发送检测中断状态命令（无参数）
// 如果返回结果表明出错，则置复位标志，
// 否则复位重新校正标志，然后再次执行软盘请求
static void recal_interrupt(void)
{
	// 发送检测中断状态命令
	output_byte(FD_SENSEI);

	// 如果返回结果字节数不等于 2 或命令
	if (result() != 2 || (ST0 & 0xE0) == 0x60)
		// 异常结束，则置复位标志
		reset = 1;
	else
		// 否则复位重新校正标志
		recalibrate = 0;

	// 执行软盘请求项
	do_fd_request();
}

// 意外软盘中断请求中断调用函数
// 首先发送检测中断状态命令（无参数）
// 如果返回结果表明出错，则置复位标志
// 否则置重新校正标志
void unexpected_floppy_interrupt(void)
{
	// 发送检测中断状态命令
	output_byte(FD_SENSEI);

	// 如果返回结果字节数不等于 2 或命令
	if (result() != 2 || (ST0 & 0xE0) == 0x60)
		// 异常结束，则置复位标志
		reset = 1;
	else
		// 否则置重新校正标志
		recalibrate = 1;
}

// 软盘重新校正处理函数
// 向软盘控制器 FDC 发送重新校正命令和参数
// 并复位重新校正标志
static void recalibrate_floppy(void)
{
	// 复位重新校正标志
	recalibrate = 0;

	// 当前磁道号归零
	current_track = 0;

	// 置软盘中断调用函数指针指向重新校正调用函数
	do_floppy = recal_interrupt;

	// 发送命令：重新校正
	output_byte(FD_RECALIBRATE);

	// 发送参数：（磁头号加）当前驱动器号
	output_byte(head << 2 | current_drive);
	if (reset)
		// 如果出错(复位标志被置位)则继续执行软盘请求
		do_fd_request();
}

// 软盘控制器 FDC 复位中断调用函数，在软盘中断处理程序中调用
// 首先发送检测中断状态命令（无参数），然后读出返回的结果字节
// 接着发送设定软驱参数命令和相关参数，最后再次调用执行软盘请求
static void reset_interrupt(void)
{
	// 发送检测中断状态命令
	output_byte(FD_SENSEI);

	// 读取命令执行结果字节
	(void)result();

	// 发送设定软驱参数命令
	output_byte(FD_SPECIFY);

	// 发送参数
	output_byte(cur_spec1); /* hut etc */
	output_byte(6);			/* Head load time =6ms, DMA */

	// 调用执行软盘请求
	do_fd_request();
}

// FDC 复位是通过将数字输出寄存器(DOR)位 2 置 0 一会儿实现的

// 复位软盘控制器
static void reset_floppy(void)
{
	int i;

	// 复位标志置0
	reset = 0;
	cur_spec1 = -1;
	cur_rate = -1;

	// 重新校正标志置位
	recalibrate = 1;

	// 显示执行软盘复位操作信息
	printk("Reset-floppy called\n\r");

	// 关中断
	cli();

	// 设置在软盘中断处理程序中调用的函数
	do_floppy = reset_interrupt;

	// 对软盘控制器 FDC 执行复位操作
	outb_p(current_DOR & ~0x04, FD_DOR);

	// 空操作，延迟
	for (i = 0; i < 100; i++)
		__asm__("nop");

	// 再启动软盘控制器
	outb(current_DOR, FD_DOR);

	// 开中断
	sti();
}

// 软驱启动定时中断调用函数
// 首先检查数字输出寄存器(DOR)，使其选择当前指定的驱动器
// 然后调用执行软盘读写传输函数 transfer()
static void floppy_on_interrupt(void)
{
	// 我们不能任意设置选择的软驱，因为这样做可能会引起进程睡眠，我们只是迫使它自己选择

	// 置已选定当前驱动器标志
	selected = 1;

	// 如果当前驱动器号与数字输出寄存器 DOR 中的不同
	// 则需要重新设置 DOR 为当前驱动器 current_drive
	// 定时延迟2 个滴答时间，然后调用软盘读写传输函数transfer()
	// 否则直接调用软盘读写传输函数
	if (current_drive != (current_DOR & 3))
	{
		current_DOR &= 0xFC;
		current_DOR |= current_drive;

		// 向数字输出寄存器输出当前 DOR
		outb(current_DOR, FD_DOR);

		// 添加定时器并执行传输函数
		add_timer(2, &transfer);
	}
	else
		// 执行软盘读写传输函数
		transfer();
}

// 软盘读写请求项处理函数
void do_fd_request(void)
{
	unsigned int block;

	// 如果复位标志已置位，则执行软盘复位操作，并返回
	seek = 0;
	if (reset)
	{
		reset_floppy();
		return;
	}

	// 如果重新校正标志已置位，则执行软盘重新校正操作，并返回
	if (recalibrate)
	{
		recalibrate_floppy();
		return;
	}

	// 检测请求项的合法性，若已没有请求项则退出
	INIT_REQUEST;

	// 将请求项结构中软盘设备号中的软盘类型 (MINOR(CURRENT->dev)>>2)
	// 作为索引取得软盘参数块
	floppy = (MINOR(CURRENT->dev) >> 2) + floppy_type;

	// 如果当前驱动器不是请求项中指定的驱动器，则置标志 seek，表示需要进行寻道操作
	// 然后置请求项设备为当前驱动器
	if (current_drive != CURRENT_DEV)
		seek = 1;

	current_drive = CURRENT_DEV;
	// 设置读写起始扇区，因为每次读写是以块为单位（1 块为 2 个扇区）
	// 所以起始扇区需要起码比磁盘总扇区数小 2 个扇区
	// 否则结束该次软盘请求项，执行下一个请求项

	// 取当前软盘请求项中起始扇区号 ->block。
	block = CURRENT->sector;

	// 如果block+2 大于磁盘扇区总数
	if (block + 2 > floppy->size)
	{
		// 结束本次软盘请求项
		end_request(0);
		goto repeat;
	}

	// 求对应在磁道上的扇区号，磁头号，磁道号，搜寻磁道号（对于软驱读不同格式的盘）

	// 起始扇区对每磁道扇区数取模，得磁道上扇区号
	sector = block % floppy->sect;

	// 起始扇区对每磁道扇区数取整，得起始磁道数
	block /= floppy->sect;

	// 起始磁道数对磁头数取模，得操作的磁头号
	head = block % floppy->head;

	// 起始磁道数对磁头数取整，得操作的磁道号
	track = block / floppy->head;

	// 相应于驱动器中盘类型进行调整，得寻道号
	seek_track = track << floppy->stretch;

	// 如果寻道号与当前磁头所在磁道不同，则置需要寻道标志 seek
	if (seek_track != current_track)
		seek = 1;

	// 磁盘上实际扇区计数是从 1 算起
	sector++;

	// 如果请求项中是读操作，则置软盘读命令码
	if (CURRENT->cmd == READ)
		command = FD_READ;

	// 如果请求项中是写操作，则置软盘写命令码
	else if (CURRENT->cmd == WRITE)
		command = FD_WRITE;
	else
		panic("do_fd_request: unknown command");

	// 向系统添加定时器，为了能对软驱进行读写操作，需要首先启动驱动器马达并达到正常运转速度
	// 这需要一定的时间，因此这里利用 ticks_to_floppy_on() 计算启动延时时间，设定一个定时器
	// 当定时时间到时，就调用函数 floppy_on_interrupt()
	add_timer(ticks_to_floppy_on(current_drive), &floppy_on_interrupt);
}

// 软盘系统初始化
// 设置软盘块设备的请求处理函数(do_fd_request())
// 并设置软盘中断门 (int 0x26，对应硬件中断请求信号 IRQ6）
// 然后取消对该中断信号的屏蔽，允许软盘控制器 FDC 发送中断请求信号
void floppy_init(void)
{
	// = do_fd_request()
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;

	// 设置软盘中断门 int 0x26(38)
	set_trap_gate(0x26, &floppy_interrupt);

	// 复位软盘的中断请求屏蔽位，允许软盘控制器发送中断请求信号
	outb(inb_p(0x21) & ~0x40, 0x21);
}
