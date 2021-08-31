/*
 *  linux/kernel/tty_io.c
 *
 *  (C) 1991  Linus Torvalds
 */

// tty_io.c 给 tty 一种非相关的感觉，是控制台还是串行通道
// 该程序同样实现了回显、规范(熟)模式等

// Kill-line，谢谢 John T Kahl

#include <ctype.h>
#include <errno.h>
#include <signal.h>

// 下面给出相应信号在信号位图中的对应比特位
#define ALRMMASK (1 << (SIGALRM - 1)) // 警告(alarm)信号屏蔽位
#define KILLMASK (1 << (SIGKILL - 1)) // 终止(kill)信号屏蔽位
#define INTMASK (1 << (SIGINT - 1))	  // 键盘中断(int)信号屏蔽位
#define QUITMASK (1 << (SIGQUIT - 1)) // 键盘退出(quit)信号屏蔽位
#define TSTPMASK (1 << (SIGTSTP - 1)) // tty 发出的停止进程(tty stop)信号屏蔽位

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/segment.h>
#include <asm/system.h>

#define _L_FLAG(tty, f) ((tty)->termios.c_lflag & f) // 取 termios 结构中的本地模式标志
#define _I_FLAG(tty, f) ((tty)->termios.c_iflag & f) // 取 termios 结构中的输入模式标志
#define _O_FLAG(tty, f) ((tty)->termios.c_oflag & f) // 取 termios 结构中的输出模式标志

// 取termios 结构中本地模式标志集中的一个标志位
#define L_CANON(tty) _L_FLAG((tty), ICANON)	   // 取本地模式标志集中规范（熟）模式标志位
#define L_ISIG(tty) _L_FLAG((tty), ISIG)	   // 取信号标志位
#define L_ECHO(tty) _L_FLAG((tty), ECHO)	   // 取回显字符标志位
#define L_ECHOE(tty) _L_FLAG((tty), ECHOE)	   // 规范模式时，取回显擦出标志位
#define L_ECHOK(tty) _L_FLAG((tty), ECHOK)	   // 规范模式时，取 KILL 擦除当前行标志位
#define L_ECHOCTL(tty) _L_FLAG((tty), ECHOCTL) // 取回显控制字符标志位
#define L_ECHOKE(tty) _L_FLAG((tty), ECHOKE)   // 规范模式时，取 KILL 擦除行并回显标志位

// 取 termios 结构中输入模式标志中的一个标志位
#define I_UCLC(tty) _I_FLAG((tty), IUCLC) // 取输入模式标志集中大写到小写转换标志位
#define I_NLCR(tty) _I_FLAG((tty), INLCR) // 取换行符 NL 转回车符 CR 标志位
#define I_CRNL(tty) _I_FLAG((tty), ICRNL) // 取回车符 CR 转换行符 NL 标志位
#define I_NOCR(tty) _I_FLAG((tty), IGNCR) // 取忽略回车符 CR 标志位

// 取 termios 结构中输出模式标志中的一个标志位
#define O_POST(tty) _O_FLAG((tty), OPOST)	// 取输出模式标志集中执行输出处理标志
#define O_NLCR(tty) _O_FLAG((tty), ONLCR)	// 取换行符 NL 转回车换行符 CR-NL 标志
#define O_CRNL(tty) _O_FLAG((tty), OCRNL)	// 取回车符 CR 转换行符 NL 标志
#define O_NLRET(tty) _O_FLAG((tty), ONLRET) // 取换行符 NL 执行回车功能的标志
#define O_LCUC(tty) _O_FLAG((tty), OLCUC)	// 取小写转大写字符标志

// tty 数据结构的 tty_table 数组
// 其中包含三个初始化项数据
// 分别对应控制台、串口终端1 和 串口终端2 的初始化数据
struct tty_struct tty_table[] = {
	{
		{ICRNL,									  // 将输入的 CR 转换为 NL
		 OPOST | ONLCR,							  // 将输出的 NL 转 CRNL
		 0,										  // 控制模式标志初始化为0
		 ISIG | ICANON | ECHO | ECHOCTL | ECHOKE, // 本地模式标志
		 0,										  // 控制台 termio
		 INIT_C_CC},							  // 控制字符数组
		0,										  // 所属初始进程组
		0,										  // 初始停止标志
		con_write,								  // tty 写函数指针
		{0, 0, 0, 0, ""},						  // tty 控制台读队列
		{0, 0, 0, 0, ""},						  // tty 控制台写队列
		{0, 0, 0, 0, ""}						  // tty 控制台辅助(第二)队列
	},
	{{0,				   // 输入模式标志 0，无须转换
	  0,				   // 输出模式标志 0，无须转换
	  B2400 | CS8,		   // 控制模式标志，波特率 2400bps，8 位数据位
	  0,				   // 本地模式标志 0
	  0,				   // 行规程 0
	  INIT_C_CC},		   // 控制字符数组
	 0,					   // 所属初始进程组
	 0,					   // 初始停止标志
	 rs_write,			   // 串口1 tty 写函数指针
	 {0x3f8, 0, 0, 0, ""}, // 串行终端1 读缓冲队列
	 {0x3f8, 0, 0, 0, ""}, // 串行终端1 写缓冲队列
	 {0, 0, 0, 0, ""}},	   // 串行终端1 辅助缓冲队列
	{{0,				   // 输入模式标志 0，无须转换
	  0,				   // 输出模式标志 0，无须转换
	  B2400 | CS8,		   // 控制模式标志，波特率 2400bps，8 位数据位
	  0,				   // 本地模式标志 0
	  0,				   // 行规程 0
	  INIT_C_CC},		   // 控制字符数组
	 0,					   // 所属初始进程组
	 0,					   // 初始停止标志
	 rs_write,			   // 串口2 tty 写函数指针
	 {0x2f8, 0, 0, 0, ""}, // 串行终端2 读缓冲队列
	 {0x2f8, 0, 0, 0, ""}, // 串行终端2 写缓冲队列
	 {0, 0, 0, 0, ""}}};   // 串行终端2 辅助缓冲队列

// 下面是汇编程序使用的缓冲队列地址表
// 通过修改你可以实现伪 tty 终端或其它终端类型，目前还没有这样做

// tty 缓冲队列地址表。rs_io.s 汇编程序使用，用于取得读写缓冲队列地址
struct tty_queue *table_list[] = {
	// 控制台终端读、写缓冲队列地址
	&tty_table[0].read_q, &tty_table[0].write_q,

	// 串行口1 终端读、写缓冲队列地址
	&tty_table[1].read_q, &tty_table[1].write_q,

	// 串行口2 终端读、写缓冲队列地址
	&tty_table[2].read_q, &tty_table[2].write_q};

// tty 终端初始化函数
// 初始化串口终端和控制台终端
void tty_init(void)
{
	// 初始化串行中断程序和串行接口1 和 2
	rs_init();

	// 初始化控制台终端
	con_init();
}

// tty 键盘中断（^C）字符处理函数
// 向 tty 结构中指明的（前台）进程组中，所有的进程发送指定的信号 mask，通常该信号是 SIGINT
// 参数：
// tty - 相应 tty 终端结构指针；
// mask - 信号屏蔽位
void tty_intr(struct tty_struct *tty, int mask)
{
	int i;

	// 如果 tty 所属进程组号小于等于 0，则退出
	// 当 pgrp=0 时，表明进程是初始进程 init，它没有控制终端，因此不应该会发出中断字符
	if (tty->pgrp <= 0)
		return;

	// 扫描任务数组，向 tty 指明的进程组（前台进程组）中所有进程发送指定的信号
	for (i = 0; i < NR_TASKS; i++)
		// 如果该项任务指针不为空，并且其组号等于 tty 组号，则设置（发送）该任务指定的信号 mask
		if (task[i] && task[i]->pgrp == tty->pgrp)
			task[i]->signal |= mask;
}

// 如果队列缓冲区空则让进程进入可中断的睡眠状态
// 参数：queue - 指定队列的指针
// 进程在取队列缓冲区中字符时调用此函数
static void sleep_if_empty(struct tty_queue *queue)
{
	// 关中断
	cli();

	// 若当前进程没有信号要处理并且指定的队列缓冲区空
	// 则让进程进入可中断睡眠状态，并让队列的进程等待指针指向该进程
	while (!current->signal && EMPTY(*queue))
		interruptible_sleep_on(&queue->proc_list);

	// 开中断
	sti();
}

// 若队列缓冲区满则让进程进入可中断的睡眠状态
// 参数：queue - 指定队列的指针
// 进程在往队列缓冲区中写入时调用此函数
static void sleep_if_full(struct tty_queue *queue)
{
	// 若队列缓冲区不满，则返回退出
	if (!FULL(*queue))
		return;

	// 关中断
	cli();

	// 如果进程没有信号需要处理，并且队列缓冲区中空闲剩余区长度<128
	// 则让进程进入可中断睡眠状态，并让该队列的进程等待指针指向该进程
	while (!current->signal && LEFT(*queue) < 128)
		interruptible_sleep_on(&queue->proc_list);

	// 开中断
	sti();
}

// 等待按键
// 如果控制台的读队列缓冲区空，则让进程进入可中断的睡眠状态
void wait_for_keypress(void)
{
	sleep_if_empty(&tty_table[0].secondary);
}

// 复制成规范模式字符序列
// 将指定 tty 终端队列缓冲区中的字符，复制成规范(熟)模式字符并存放在辅助队列(规范模式队列)中
// 参数：tty - 指定终端的 tty 结构
void copy_to_cooked(struct tty_struct *tty)
{
	signed char c;

	// 如果 tty 的读队列缓冲区不空，并且辅助队列缓冲区为空，则循环执行下列代码
	while (!EMPTY(tty->read_q) && !FULL(tty->secondary))
	{
		// 从读队列尾处取一字符到 c，并前移尾指针
		GETCH(tty->read_q, c);

		// 下面对输入字符，利用输入模式标志集进行处理
		// 如果该字符是回车符CR(13)
		// 则：若回车转换行标志 CRNL 置位，则将该字符转换为换行符 NL(10)；
		// 否则若忽略回车标志NOCR 置位，则忽略该字符，继续处理其它字符
		if (c == 13)
			if (I_CRNL(tty))
				c = 10;
			else if (I_NOCR(tty))
				continue;
			else
				;

		// 如果该字符是换行符 NL(10) 并且换行转回车标志 NLCR 置位，则将其转换为回车符 CR(13)
		else if (c == 10 && I_NLCR(tty))
			c = 13;

		// 如果大写转小写标志 UCLC 置位，则将该字符转换为小写字符
		if (I_UCLC(tty))
			c = tolower(c);

		// 如果本地模式标志集中规范（熟）模式标志 CANON 置位，则进行以下处理
		if (L_CANON(tty))
		{
			// 如果该字符是键盘终止控制字符 KILL(^U)，则进行删除输入行上所有字符的处理
			if (c == KILL_CHAR(tty))
			{
				/* deal with killing the input line */
				// 删除输入行处理

				// 如果 tty 辅助队列不空，或者辅助队列中最后一个字符是换行NL(10)
				// 或者该字符是文件结束字符(^D)，则循环执行下列代码
				while (!(EMPTY(tty->secondary) ||
						 (c = LAST(tty->secondary)) == 10 ||
						 c == EOF_CHAR(tty)))
				{
					// 如果本地回显标志 ECHO 置位
					// 那么：若字符是控制字符(值<32)，则往 tty 的写队列中放入擦除字符 ERASE
					// 再放入一个擦除字符 ERASE，并且调用该 tty 的写函数
					// 该写函数就会把写队列中的字符输出到终端的屏幕上
					// 另外，因为控制字符在放入写队列时是用两个字符表示的（例如^V）
					// 因此需要特别对控制字符多放入一个 ERASE
					if (L_ECHO(tty))
					{
						if (c < 32)
							PUTCH(127, tty->write_q);
						PUTCH(127, tty->write_q);
						tty->write(tty);
					}
					// 将 tty 辅助队列头指针后退 1 字节
					DEC(tty->secondary.head);
				}
				// 继续读取并处理其它字符
				continue;
			}

			// 如果该字符是删除控制字符 ERASE(^H)
			if (c == ERASE_CHAR(tty))
			{
				// 若 tty 的辅助队列为空，或者其最后一个字符是换行符NL(10)
				// 或者是文件结束符，则继续处理其它字符
				if (EMPTY(tty->secondary) ||
					(c = LAST(tty->secondary)) == 10 ||
					c == EOF_CHAR(tty))
					continue;

				// 如果本地回显标志 ECHO 置位，那么：若字符是控制字符(值<32)
				// 则往 tty 的写队列中放入擦除字符 ERASE
				// 再放入一个擦除字符 ERASE，并且调用该 tty 的写函数
				if (L_ECHO(tty))
				{
					if (c < 32)
						PUTCH(127, tty->write_q);
					PUTCH(127, tty->write_q);
					tty->write(tty);
				}

				// 将 tty 辅助队列头指针后退 1 字节，继续处理其它字符
				DEC(tty->secondary.head);
				continue;
			}

			//如果该字符是停止字符(^S)，则置 tty 停止标志，继续处理其它字符
			if (c == STOP_CHAR(tty))
			{
				tty->stopped = 1;
				continue;
			}

			// 如果该字符是开始字符(^Q)，则复位 tty 停止标志，继续处理其它字符
			if (c == START_CHAR(tty))
			{
				tty->stopped = 0;
				continue;
			}
		}

		// 若输入模式标志集中 ISIG 标志置位，表示终端键盘可以产生信号
		// 则在收到 INTR、QUIT、SUSP 或 DSUSP 字符时，需要为进程产生相应的信号
		if (L_ISIG(tty))
		{
			// 如果该字符是键盘中断符(^C)，则向当前进程发送键盘中断信号，并继续处理下一字符
			if (c == INTR_CHAR(tty))
			{
				tty_intr(tty, INTMASK);
				continue;
			}

			// 如果该字符是退出符(^\)，则向当前进程发送键盘退出信号，并继续处理下一字符
			if (c == QUIT_CHAR(tty))
			{
				tty_intr(tty, QUITMASK);
				continue;
			}
		}

		// 如果该字符是换行符 NL(10)，或者是文件结束符 EOF(4，^D)
		// 表示一行字符已处理完，则把辅助缓冲队列中含有字符行数值增 1
		// 在 tty_read() 中若取走一行字符，就会将其值减 1
		if (c == 10 || c == EOF_CHAR(tty))
			tty->secondary.data++;

		// 如果本地模式标志集中回显标志 ECHO 置位，那么，如果字符是换行符 NL(10)
		// 则将换行符 NL(10) 和回车符 CR(13) 放入 tty 写队列缓冲区中；
		// 如果字符是控制字符(字符值<32)并且回显控制字符标志 ECHOCTL 置位
		// 则将字符'^'和字符 c+64 放入 tty 写队列中(也即会显示^C、^H 等)；
		// 否则将该字符直接放入 tty 写缓冲队列中，最后调用该 tty 的写操作函数
		if (L_ECHO(tty))
		{
			if (c == 10)
			{
				PUTCH(10, tty->write_q);
				PUTCH(13, tty->write_q);
			}
			else if (c < 32)
			{
				if (L_ECHOCTL(tty))
				{
					PUTCH('^', tty->write_q);
					PUTCH(c + 64, tty->write_q);
				}
			}
			else
				PUTCH(c, tty->write_q);
			tty->write(tty);
		}
		// 将该字符放入辅助队列中
		PUTCH(c, tty->secondary);
	}
	// 唤醒等待该辅助缓冲队列的进程（如果有的话）
	wake_up(&tty->secondary.proc_list);
}

// tty 读函数，从终端辅助缓冲队列中读取指定数量的字符，放到用户指定的缓冲区中
// 参数：
// channel - 子设备号；
// buf – 用户缓冲区指针；
// nr - 欲读字节数；
// 返回已读字节数
int tty_read(unsigned channel, char *buf, int nr)
{
	struct tty_struct *tty;
	char c, *b = buf;
	int minimum, time, flag = 0;
	long oldalarm;

	// 本版本 linux 内核的终端只有 3 个子设备，分别是 控制台(0)、串口终端1(1) 和 串口终端2(2)
	// 所以任何大于 2 的子设备号都是非法的，读的字节数当然也不能小于 0 的
	if (channel > 2 || nr < 0)
		return -1;

	// tty 指针指向子设备号对应 ttb_table 表中的 tty 结构
	tty = &tty_table[channel];

	// 下面首先保存进程原定时值，然后根据控制字符 VTIME 和 VMIN 设置读字符操作的超时定时值
	// 在非规范模式下，这两个值是超时定时值
	// MIN 表示为了满足读操作，需要读取的最少字符数
	// TIME 是一个十分之一秒计数的计时值
	// 首先保存进程当前的(报警)定时值(滴答数)
	oldalarm = current->alarm;

	// 并设置读操作超时定时值 time 和需要最少读取的字符个数 minimum
	time = 10L * tty->termios.c_cc[VTIME];
	minimum = tty->termios.c_cc[VMIN];

	// 如果设置了读超时定时值 time 但没有设置最少读取个数 minimum
	// 那么在读到至少一个字符或者定时超时后读操作将立刻返回，所以这里置 minimum=1
	if (time && !minimum)
	{
		minimum = 1;
		// 如果进程原定时值是 0 或者 time+ 当前系统时间值 小于 进程原定时值的话
		// 则置重新设置进程定时值为 time + 当前系统时间，并置 flag 标志
		if ((flag = (!oldalarm || time + jiffies < oldalarm)))
			current->alarm = time + jiffies;
	}

	// 如果设置的最少读取字符数>欲读的字符数，则令其等于此次欲读取的字符数
	if (minimum > nr)
		minimum = nr;

	// 当欲读的字节数>0，则循环执行以下操作
	while (nr > 0)
	{
		// 如果 flag 不为 0(也即进程原定时值是 0 或者 time + 当前系统时间值 小于进程原定时值)
		// 并且进程此时以收到定时信号 SIGALRM，表明这里设置的定时时间已到
		// 则复位进程的定时信号 SIGALRM，并中断循环
		if (flag && (current->signal & ALRMMASK))
		{
			current->signal &= ~ALRMMASK;
			break;
		}

		// 如果 flag 没有置位，或者已置位，但当前进程有其它信号要处理，则退出，返回 0
		if (current->signal)
			break;

		// 如果辅助缓冲队列(规范模式队列)为空
		// 或者设置了规范模式标志，并且辅助队列中字符数为0
		// 以及辅助模式缓冲队列空闲空间>20，则进入可中断睡眠状态，返回后继续处理
		if (EMPTY(tty->secondary) || (L_CANON(tty) &&
									  !tty->secondary.data && LEFT(tty->secondary) > 20))
		{
			sleep_if_empty(&tty->secondary);
			continue;
		}
		// 执行以下取字符操作，需读字符数 nr 依次递减，直到 nr=0 或者辅助缓冲队列为空
		do
		{
			// 取辅助缓冲队列字符 c，并且缓冲队列 secondary->tail 指针向右移动一个字符位置（tail++）
			GETCH(tty->secondary, c);

			// 如果该字符是文件结束符 (^D) 或者是换行符 NL(10)
			// 则把辅助缓冲队列中含有字符行数值减 1
			if (c == EOF_CHAR(tty) || c == 10)
				tty->secondary.data--;

			// 如果该字符是文件结束符(^D)并且规范模式标志置位，则返回已读字符数，并退出
			if (c == EOF_CHAR(tty) && L_CANON(tty))
				return (b - buf);

			// 否则说明是原始模式（非规范模式）操作，于是将该字符直接放入用户数据段缓冲区 buf 中，
			// 并把欲读字符数减 1。此时如果欲读字符数已为 0，则中断循环
			else
			{
				put_fs_byte(c, b++);
				if (!--nr)
					break;
			}
		} while (nr > 0 && !EMPTY(tty->secondary));

		// 如果超时定时值 time 不为 0，并且规范模式标志没有置位(非规范模式)
		if (time && !L_CANON(tty))
		{
			// 如果进程原定时值是 0 或者 time+ 当前系统时间值 小于 进程原定时值的话
			// 则置重新设置进程定时值为 time + 当前系统时间，并置 flag 标志
			// 为读取下一个字符做好定时准备
			// 否则表明进程原定时时间要比等待一个字符被读取的定时时间要小
			// 可能没有等到字符到来进程原定时时间就到了
			// 因此，此时这里需要恢复进程的原定时值（oldalarm）
			if ((flag = (!oldalarm || time + jiffies < oldalarm)))
				current->alarm = time + jiffies;
			else
				current->alarm = oldalarm;
		}

		// 如果规范模式标志置位，那么若已读到起码一个字符则中断循环
		// 否则若已读取数大于或等于最少要求读取的字符数，则也中断循环
		if (L_CANON(tty))
		{
			if (b - buf)
				break;
		}
		else if (b - buf >= minimum)
			break;
	}

	// 读取 tty 字符循环操作结束，让进程的定时值恢复值
	current->alarm = oldalarm;

	// 如果进程有信号并且没有读取到任何字符，则返回出错号（被中断）
	if (current->signal && !(b - buf))
		return -EINTR;

	// 返回已读取的字符数
	return (b - buf);
}

// tty 写函数，把用户缓冲区中的字符写入 tty 的写队列中
// 参数：
// channel - 子设备号；
// buf - 缓冲区指针；
// nr - 写字节数；
// 返回已写字节数。
int tty_write(unsigned channel, char *buf, int nr)
{
	static int cr_flag = 0;
	struct tty_struct *tty;
	char c, *b = buf;

	// 本版本 linux 内核的终端只有 3 个子设备，分别是控制台(0)、串口终端1(1) 和 串口终端2(2)
	// 所以任何大于 2 的子设备号都是非法的，写的字节数当然也不能小于 0 的
	if (channel > 2 || nr < 0)
		return -1;

	// tty 指针指向子设备号对应 ttb_table 表中的 tty 结构
	tty = channel + tty_table;

	// 字符设备是一个一个字符进行处理的，所以这里对于 nr 大于 0 时对每个字符进行循环处理
	while (nr > 0)
	{
		// 如果此时 tty 的写队列已满，则当前进程进入可中断的睡眠状态
		sleep_if_full(&tty->write_q);

		// 如果当前进程有信号要处理，则退出，返回0
		if (current->signal)
			break;

		// 当要写的字节数>0 并且 tty 的写队列不满时，循环执行以下操作
		while (nr > 0 && !FULL(tty->write_q))
		{
			// 从用户数据段内存中取一字节 c
			c = get_fs_byte(b);

			// 如果终端输出模式标志，集中的执行输出处理标志 OPOST 置位，则执行下列输出时处理过程
			if (O_POST(tty))
			{
				// 如果该字符是回车符 '\r'(CR，13) 并且回车符转换行符标志 OCRNL 置位
				// 则将该字符换成换行符 '\n'(NL，10)；
				// 否则如果该字符是换行符 '\n'(NL，10) 并且换行转回车功能标志 ONLRET 置位的话，
				// 则将该字符换成回车符 '\r'(CR，13)；
				if (c == '\r' && O_CRNL(tty))
					c = '\n';
				else if (c == '\n' && O_NLRET(tty))
					c = '\r';

				// 如果该字符是换行符 '\n' 并且回车标志 cr_flag 没有置位
				// 换行转回车-换行标志 ONLCR 置位的话，
				// 则将 cr_flag 置位，并将一回车符放入写队列中，然后继续处理下一个字符
				if (c == '\n' && !cr_flag && O_NLCR(tty))
				{
					cr_flag = 1;
					PUTCH(13, tty->write_q);
					continue;
				}

				// 如果小写转大写标志 OLCUC 置位的话，就将该字符转成大写字符
				if (O_LCUC(tty))
					c = toupper(c);
			}

			// 用户数据缓冲指针 b 前进 1 字节；
			// 欲写字节数减 1 字节；
			// 复位 cr_flag 标志
			// 并将该字节放入 tty 写队列中
			b++;
			nr--;
			cr_flag = 0;
			PUTCH(c, tty->write_q);
		}

		// 若字节全部写完，或者写队列已满，则程序执行到这里
		// 调用对应 tty 的写函数，若还有字节要写
		// 则等待写队列不满，所以调用调度程序，先去执行其它任务
		tty->write(tty);
		if (nr > 0)
			schedule();
	}
	return (b - buf);
}

// 呵，有时我是真得很喜欢 386，该子程序是从一个中断处理程序中调用的，
// 即使在中断处理程序中睡眠也应该绝对没有问题(我希望如此)
// 当然，如果有人证明我是错的，那么我将憎恨 intel 一辈子 😊
// 但是我们必须小心，在调用该子程序之前需要恢复中断

// 我不认为在通常环境下会处在这里睡眠，这样很好，因为任务睡眠是完全任意的

// tty 中断处理调用函数 - 执行 tty 中断处理
// 参数：tty - 指定的tty 终端号（0，1 或 2）
// 将指定 tty 终端队列缓冲区中的字符，复制成规范(熟)模式字符，并存放在辅助队列(规范模式队列)中
// 在串口读字符中断 (rs_io.s) 和键盘中断 (kerboard.S) 中调用
void do_tty_interrupt(int tty)
{
	copy_to_cooked(tty_table + tty);
}

// 字符设备初始化函数，空，为以后扩展做准备
void chr_dev_init(void)
{
}
