/*
 *  linux/kernel/console.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 该模块实现控制台输入输出功能
// 'void con_init(void)'
// 'void con_write(struct tty_queue * queue)'
// 希望这是一个非常完整的 VT102 实现

// 感谢 John T Kohl 实现了蜂鸣指示

// 注意!!! 我们有时短暂地禁止和允许中断(在将一个字(word)放到视频IO)
// 但即使对于键盘中断这也是可以工作的，因为我们使用陷阱门
// 所以我们知道在获得一个键盘中断时中断是不允许的，希望一切均正常

// 检测不同显示卡的代码大多数是 Galen Hunt 编写的，<g-hunt@ee.utah.edu>

#include <linux/sched.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/system.h>

// 这些是设置子程序 setup 在引导启动系统时设置的参数：

// 参见对 boot/setup.s 的注释，和 setup 程序读取并保留的参数表
#define ORIG_X (*(unsigned char *)0x90000)							   // 光标列号
#define ORIG_Y (*(unsigned char *)0x90001)							   // 光标行号
#define ORIG_VIDEO_PAGE (*(unsigned short *)0x90004)				   // 显示页面
#define ORIG_VIDEO_MODE ((*(unsigned short *)0x90006) & 0xff)		   // 显示模式
#define ORIG_VIDEO_COLS (((*(unsigned short *)0x90006) & 0xff00) >> 8) // 字符列数
#define ORIG_VIDEO_LINES (25)										   // 显示行数
#define ORIG_VIDEO_EGA_AX (*(unsigned short *)0x90008)				   // [??]
#define ORIG_VIDEO_EGA_BX (*(unsigned short *)0x9000a)				   // 显示内存大小和色彩模式
#define ORIG_VIDEO_EGA_CX (*(unsigned short *)0x9000c)				   // 显示卡特性参数

// 定义显示器单色/彩色显示模式类型符号常数

// 单色文本
#define VIDEO_TYPE_MDA 0x10 /* Monochrome Text Display	*/

// CGA 显示器
#define VIDEO_TYPE_CGA 0x11 /* CGA Display 			*/

// EGA/VGA 单色
#define VIDEO_TYPE_EGAM 0x20 /* EGA/VGA in Monochrome Mode	*/

// EGA/VGA 彩色
#define VIDEO_TYPE_EGAC 0x21 /* EGA/VGA in Color Mode	*/

#define NPAR 16

// 键盘中断处理程序
extern void keyboard_interrupt(void);

// 使用的显示类型
static unsigned char video_type; /* Type of display being used	*/

// 屏幕文本列数
static unsigned long video_num_columns; /* Number of text columns	*/

// 每行使用的字节数
static unsigned long video_size_row; /* Bytes per row		*/

// 屏幕文本行数
static unsigned long video_num_lines; /* Number of test lines		*/

// 初始显示页面
static unsigned char video_page; /* Initial video page		*/

// 显示内存起始地址
static unsigned long video_mem_start; /* Start of video RAM		*/

// 显示内存结束(末端)地址
static unsigned long video_mem_end; /* End of video RAM (sort of)	*/

// 显示控制索引寄存器端口
static unsigned short video_port_reg; /* Video register select port	*/

// 显示控制数据寄存器端口
static unsigned short video_port_val; /* Video register value port	*/

// 擦除字符属性与字符(0x0720)
static unsigned short video_erase_char; /* Char+Attrib to erase with	*/

// 以下这些变量用于屏幕卷屏操作

// 用于EGA/VGA 快速滚屏，滚屏起始内存地址
static unsigned long origin; /* Used for EGA/VGA fast scroll	*/

// 滚屏末端内存地址
static unsigned long scr_end; /* Used for EGA/VGA fast scroll	*/

// 当前光标对应的显示内存位置
static unsigned long pos;

// 当前光标位置
static unsigned long x, y;

// 滚动时顶行行号；底行行号
static unsigned long top, bottom;

// state 用于标明处理 ESC 转义序列时的当前步骤
// npar, par[] 用于存放 ESC 序列的中间处理参数

// ANSI 转义字符序列处理状态
static unsigned long state = 0;

// ANSI 转义字符序列参数个数和参数数组
static unsigned long npar, par[NPAR];
static unsigned long ques = 0;

// 字符属性(黑底白字)
static unsigned char attr = 0x07;

// 系统蜂鸣函数
static void sysbeep(void);

// 下面是终端回应 ESC-Z 或 csi0c 请求的应答(=vt100 响应)
// csi - 控制序列引导码(Control Sequence Introducer)
#define RESPONSE "\033[?1;2c"

// 注意！gotoxy 函数认为 x==video_num_columns，这是正确的
// 跟踪光标当前位置
// 参数：new_x - 光标所在列号；new_y - 光标所在行号；
// 更新当前光标位置变量 x, y，并修正 pos 指向光标在显示内存中的对应位置
static inline void gotoxy(unsigned int new_x, unsigned int new_y)
{
	// 如果输入的光标行号超出显示器列数，或者光标行号超出显示的最大行数，则退出
	if (new_x > video_num_columns || new_y >= video_num_lines)
		return;

	// 更新当前光标变量；更新光标位置对应的在显示内存中位置变量 pos
	x = new_x;
	y = new_y;
	pos = origin + y * video_size_row + (x << 1);
}

// 设置滚屏起始显示内存地址
static inline void set_origin(void)
{
	// 关中断
	cli();

	// 首先选择显示控制数据寄存器 r12，然后写入卷屏起始地址高字节
	// 向右移动 9 位，表示向右移动 8 位，再除以2(2 字节代表屏幕上1 字符)
	// 是相对于默认显示内存操作的
	outb_p(12, video_port_reg);
	outb_p(0xff & ((origin - video_mem_start) >> 9), video_port_val);

	// 再选择显示控制数据寄存器r13，然后写入卷屏起始地址底字节。向右移动 1 位表示除以 2
	outb_p(13, video_port_reg);
	outb_p(0xff & ((origin - video_mem_start) >> 1), video_port_val);

	// 开中断
	sti();
}

// 向上卷动一行（屏幕窗口向下移动）
// 将屏幕窗口向下移动一行
static void scrup(void)
{
	// 如果显示类型是 EGA，则执行以下操作
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		// 如果移动起始行 top=0
		// 移动最底行 bottom=video_num_lines=25，则表示整屏窗口向下移动
		if (!top && bottom == video_num_lines)
		{
			// 调整屏幕显示对应内存的起始位置指针 origin 为向下移一行屏幕字符对应的内存位置
			// 同时也调整当前光标对应的内存位置，以及屏幕末行末端字符指针 scr_end 的位置
			origin += video_size_row;
			pos += video_size_row;
			scr_end += video_size_row;

			// 如果屏幕末端最后一个显示字符，所对应的显示内存指针 scr_end 超出了实际显示内存的末端
			// 则将屏幕内容内存数据移动到显示内存的起始位置 video_mem_start
			// 并在出现的新行上填入空格字符
			if (scr_end > video_mem_end)
			{
				// %0 - eax(擦除字符+属性)；
				// %1 - ecx((显示器字符行数-1)所对应的字符数/2，是以长字移动)；
				// %2 - edi(显示内存起始位置video_mem_start)；
				// %3 - esi(屏幕内容对应的内存起始位置origin)；
				// 移动方向：[edi] -> [esi]，移动 ecx 个双字
				__asm__("cld\n\t"
						"rep\n\t"
						"movsl\n\t"
						"movl video_num_columns,%1\n\t"
						"rep\n\t"
						"stosw" ::"a"(video_erase_char),
						"c"((video_num_lines - 1) * video_num_columns >> 1),
						"D"(video_mem_start),
						"S"(origin));

				// 根据屏幕内存数据移动后的情况
				// 重新调整当前屏幕对应内存的起始指针
				// 光标位置指针和屏幕末端对应内存指针 scr_end
				scr_end -= origin - video_mem_start;
				pos -= origin - video_mem_start;
				origin = video_mem_start;
			}
			else
			{
				// 如果调整后的屏幕末端对应的内存指针 scr_end 没有超出显示内存的末端 video_mem_end
				// 则只需在新行上填入擦除字符(空格字符)
				__asm__("cld\n\t"
						"rep\n\t"
						"stosw" ::"a"(video_erase_char),
						"c"(video_num_columns),
						"D"(scr_end - video_size_row));
			}
			// 向显示控制器中写入新的屏幕内容对应的内存起始位置值
			set_origin();
		}
		else
		{
			// 否则表示不是整屏移动。也即表示从指定行 top 开始的所有行向上移动 1 行(删除 1 行)
			// 此时直接将屏幕从指定行 top 到屏幕末端，所有行对应的显示内存数据向上移动 1 行
			// 并在新出现的行上填入擦除字符
			// %0-eax(擦除字符+属性)；
			// %1-ecx(top 行下 1 行开始到屏幕末行的行数所对应的内存长字数)；
			// %2-edi(top 行所处的内存位置)；
			// %3-esi(top+1 行所处的内存位置)；
			__asm__("cld\n\t"
					"rep\n\t"
					"movsl\n\t"
					"movl video_num_columns,%%ecx\n\t"
					"rep\n\t"
					"stosw" ::"a"(video_erase_char),
					"c"((bottom - top - 1) * video_num_columns >> 1),
					"D"(origin + video_size_row * top),
					"S"(origin + video_size_row * (top + 1)));
		}
	}

	// 如果显示类型不是EGA(是MDA)，则执行下面移动操作
	// 因为 MDA 显示控制卡会自动调整超出显示范围的情况，也即会自动翻卷指针
	// 所以这里不对屏幕内容对应内存超出显示内存的情况单独处理
	// 处理方法与 EGA 非整屏移动情况完全一样
	else /* Not EGA/VGA */
	{

		__asm__("cld\n\t"
				"rep\n\t"
				"movsl\n\t"
				"movl video_num_columns,%%ecx\n\t"
				"rep\n\t"
				"stosw" ::"a"(video_erase_char),
				"c"((bottom - top - 1) * video_num_columns >> 1),
				"D"(origin + video_size_row * top),
				"S"(origin + video_size_row * (top + 1)));
	}
}

// 向下卷动一行（屏幕窗口向上移动）
// 将屏幕窗口向上移动一行，屏幕显示的内容向下移动 1 行，在被移动开始行的上方出现一新行
// 处理方法与 scrup() 相似，只是为了在移动显示内存数据时不出现数据覆盖错误情况
// 复制是以反方向进行的，也即从屏幕倒数第 2 行的最后一个字符开始复制
static void scrdown(void)
{
	// 如果显示类型是 EGA，则执行下列操作
	// [??好象if 和else 的操作完全一样啊!为什么还要分别处理呢？难道与任务切换有关？]
	if (video_type == VIDEO_TYPE_EGAC || video_type == VIDEO_TYPE_EGAM)
	{
		// %0-eax(擦除字符+属性)；
		// %1-ecx(top 行开始到屏幕末行-1 行的行数所对应的内存长字数)；
		// %2-edi(屏幕右下角最后一个长字位置)；
		// %3-esi(屏幕倒数第2 行最后一个长字位置)；
		// 移动方向：[esi] -> [edi]，移动 ecx 个双字
		__asm__("std\n\t"
				"rep\n\t"
				"movsl\n\t"
				"addl $2,%%edi\n\t" /* %edi has been decremented by 4 */
				"movl video_num_columns,%%ecx\n\t"
				"rep\n\t"
				"stosw" ::"a"(video_erase_char),
				"c"((bottom - top - 1) * video_num_columns >> 1),
				"D"(origin + video_size_row * bottom - 4),
				"S"(origin + video_size_row * (bottom - 1) - 4));
	}
	// 如果不是 EGA 显示类型，则执行以下操作（目前与上面完全一样）
	else /* Not EGA/VGA */
	{
		__asm__("std\n\t"
				"rep\n\t"
				"movsl\n\t"
				"addl $2,%%edi\n\t" /* %edi has been decremented by 4 */
				"movl video_num_columns,%%ecx\n\t"
				"rep\n\t"
				"stosw" ::"a"(video_erase_char),
				"c"((bottom - top - 1) * video_num_columns >> 1),
				"D"(origin + video_size_row * bottom - 4),
				"S"(origin + video_size_row * (bottom - 1) - 4));
	}
}

// 光标位置下移一行(lf - line feed 换行)
static void lf(void)
{
	// 如果光标没有处在倒数第 2 行之后，则直接修改光标当前行变量 y++
	// 并调整光标对应显示内存位置 pos(加上屏幕一行字符所对应的内存长度)
	if (y + 1 < bottom)
	{
		y++;
		pos += video_size_row;
		return;
	}

	// 否则需要将屏幕内容上移一行
	scrup();
}

// 光标上移一行(ri - reverse line feed 反向换行)
static void ri(void)
{
	// 如果光标不在第 1 行上，则直接修改光标当前行标量 y--
	// 并调整光标对应显示内存位置 pos，减去屏幕上一行字符所对应的内存长度字节数
	if (y > top)
	{
		y--;
		pos -= video_size_row;
		return;
	}
	// 否则需要将屏幕内容下移一行
	scrdown();
}

// 光标回到第 1 列(0 列)左端(cr - carriage return 回车)
static void cr(void)
{
	// 光标所在的 列号*2 即 0 列到光标所在列对应的内存字节长度
	pos -= x << 1;
	x = 0;
}

// 擦除光标前一字符(用空格替代)(del - delete 删除)
static void del(void)
{
	// 如果光标没有处在 0 列，则将光标对应内存位置指针 pos 后退 2 字节(对应屏幕上一个字符)
	// 然后将当前光标变量列值减 1，并将光标所在位置字符擦除
	if (x)
	{
		pos -= 2;
		x--;
		*(unsigned short *)pos = video_erase_char;
	}
}

// 删除屏幕上与光标位置相关的部分，以屏幕为单位
// csi - 控制序列引导码(Control Sequence Introducer)
// ANSI 转义序列：'ESC [sJ'(s = 0 删除光标到屏幕底端；1 删除屏幕开始到光标处；2 整屏删除)
// 参数：par - 对应上面 s
static void csi_J(int par)
{
	long count;
	long start;

	// 首先根据三种情况，分别设置需要删除的字符数和删除开始的显示内存位置
	switch (par)
	{
		// 擦除光标到屏幕底端
	case 0: /* erase from cursor to end of display */
		count = (scr_end - pos) >> 1;
		start = pos;
		break;

		// 删除从屏幕开始到光标处的字符
	case 1: /* erase from start to cursor */
		count = (pos - origin) >> 1;
		start = origin;
		break;

		// 删除整个屏幕上的字符
	case 2: /* erase whole display */
		count = video_num_columns * video_num_lines;
		start = origin;
		break;
	default:
		return;
	}

	// 然后使用擦除字符填写删除字符的地方
	// %0 - ecx(要删除的字符数count)；
	// %1 - edi(删除操作开始地址)；
	// %2 - eax（填入的擦除字符）；
	__asm__("cld\n\t"
			"rep\n\t"
			"stosw\n\t" ::"c"(count),
			"D"(start), "a"(video_erase_char));
}

// 删除行内与光标位置相关的部分，以一行为单位
// ANSI 转义字符序列：'ESC [sK'(s = 0 删除到行尾；1 从开始删除；2 整行都删除)
static void csi_K(int par)
{
	long count;
	long start;

	// 首先根据三种情况，分别设置需要删除的字符数和删除开始的显示内存位置
	switch (par)
	{
		// 删除光标到行尾字符
	case 0: /* erase from cursor to end of line */
		if (x >= video_num_columns)
			return;
		count = video_num_columns - x;
		start = pos;
		break;

		// 删除从行开始到光标处
	case 1: /* erase from start of line to cursor */
		start = pos - (x << 1);
		count = (x < video_num_columns) ? x : video_num_columns;
		break;

		// 将整行字符全删除
	case 2: /* erase whole line */
		start = pos - (x << 1);
		count = video_num_columns;
		break;
	default:
		return;
	}
	// 然后使用擦除字符填写删除字符的地方
	// %0 - ecx(要删除的字符数count)；
	// %1 - edi(删除操作开始地址)；
	// %2 - eax（填入的擦除字符）
	__asm__("cld\n\t"
			"rep\n\t"
			"stosw\n\t" ::"c"(count),
			"D"(start), "a"(video_erase_char));
}

// 允许翻译(重显)（允许重新设置字符显示方式，比如加粗、加下划线、闪烁、反显等）
// ANSI 转义字符序列：'ESC [nm'。n = 0 正常显示；1 加粗；4 加下划线；7 反显；27 正常显示；
void csi_m(void)
{
	int i;

	for (i = 0; i <= npar; i++)
		switch (par[i])
		{
		case 0:
			attr = 0x07;
			break;
		case 1:
			attr = 0x0f;
			break;
		case 4:
			attr = 0x0f;
			break;
		case 7:
			attr = 0x70;
			break;
		case 27:
			attr = 0x07;
			break;
		}
}

// 根据设置显示光标
// 根据显示内存光标对应位置 pos，设置显示控制器光标的显示位置
static inline void set_cursor(void)
{
	// 关中断
	cli();

	// 首先使用索引寄存器端口选择显示控制数据寄存器 r14(光标当前显示位置高字节)
	// 然后写入光标当前位置高字节(向右移动 9 位表示高字节移到低字节再除以 2)，是相对于默认显示内存操作的
	outb_p(14, video_port_reg);
	outb_p(0xff & ((pos - video_mem_start) >> 9), video_port_val);

	// 再使用索引寄存器选择 r15，并将光标当前位置低字节写入其中
	outb_p(15, video_port_reg);
	outb_p(0xff & ((pos - video_mem_start) >> 1), video_port_val);

	// 开中断
	sti();
}

// 发送对终端 VT100 的响应序列
// 将响应序列放入读缓冲队列中
static void respond(struct tty_struct *tty)
{
	char *p = RESPONSE;

	// 关中断
	cli();

	// 将字符序列放入写队列
	while (*p)
	{
		PUTCH(*p, tty->read_q);
		p++;
	}

	// 开中断
	sti();

	// 转换成规范模式(放入辅助队列中)
	copy_to_cooked(tty);
}

// 在光标处插入一空格字符
static void insert_char(void)
{
	int i = x;
	unsigned short tmp, old = video_erase_char;
	unsigned short *p = (unsigned short *)pos;

	// 光标开始的所有字符右移一格，并将擦除字符插入在光标所在处
	// 若一行上都有字符的话，则行最后一个字符将不会更动 😊?
	while (i++ < video_num_columns)
	{
		tmp = *p;
		*p = old;
		old = tmp;
		p++;
	}
}

// 在光标处插入一行（则光标将处在新的空行上）
// 将屏幕从光标所在行到屏幕底向下卷动一行
static void insert_line(void)
{
	int oldtop, oldbottom;

	// 保存原 top，bottom 值
	oldtop = top;
	oldbottom = bottom;

	// 设置屏幕卷动开始行
	top = y;

	// 设置屏幕卷动最后行
	bottom = video_num_lines;

	// 从光标开始处，屏幕内容向下滚动一行
	scrdown();

	// 恢复原 top，bottom 值
	top = oldtop;
	bottom = oldbottom;
}

// 删除光标处的一个字符
static void delete_char(void)
{
	int i;
	unsigned short *p = (unsigned short *)pos;

	// 如果光标超出屏幕最右列，则返回
	if (x >= video_num_columns)
		return;

	// 从光标右一个字符开始到行末所有字符左移一格
	i = x;
	while (++i < video_num_columns)
	{
		*p = *(p + 1);
		p++;
	}
	// 最后一个字符处填入擦除字符(空格字符)
	*p = video_erase_char;
}

// 删除光标所在行
// 从光标所在行开始屏幕内容上卷一行
static void delete_line(void)
{
	int oldtop, oldbottom;

	// 保存原 top，bottom 值
	oldtop = top;
	oldbottom = bottom;

	// 设置屏幕卷动开始行
	top = y;

	// 设置屏幕卷动最后行
	bottom = video_num_lines;

	// 从光标开始处，屏幕内容向上滚动一行
	scrup();

	// 恢复原 top，bottom 值
	top = oldtop;
	bottom = oldbottom;
}

// 在光标处插入 nr 个字符
// ANSI 转义字符序列：'ESC [n@ '
// 参数 nr = 上面n
static void csi_at(unsigned int nr)
{
	// 如果插入的字符数大于一行字符数，则截为一行字符数；
	// 若插入字符数 nr 为 0，则插入 1 个字符；
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;

	// 循环插入指定的字符数
	while (nr--)
		insert_char();
}

// 在光标位置处插入 nr 行
// ANSI 转义字符序列'ESC [nL'
static void csi_L(unsigned int nr)
{
	// 如果插入的行数大于屏幕最多行数，则截为屏幕显示行数；
	// 若插入行数 nr 为 0，则插入 1 行
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;
	// 循环插入指定行数 nr
	while (nr--)
		insert_line();
}

// 删除光标处的 nr 个字符
// ANSI 转义序列：'ESC [nP'
static void csi_P(unsigned int nr)
{
	// 如果删除的字符数大于一行字符数，则截为一行字符数；
	// 若删除字符数 nr 为 0，则删除 1 个字符；
	if (nr > video_num_columns)
		nr = video_num_columns;
	else if (!nr)
		nr = 1;

	// 循环删除指定字符数 nr
	while (nr--)
		delete_char();
}

// 删除光标处的 nr 行
// ANSI 转义序列：'ESC [nM'
static void csi_M(unsigned int nr)
{
	// 如果删除的行数大于屏幕最多行数，则截为屏幕显示行数；
	// 若删除的行数 nr 为 0，则删除 1 行；
	if (nr > video_num_lines)
		nr = video_num_lines;
	else if (!nr)
		nr = 1;

	// 循环删除指定行数 nr
	while (nr--)
		delete_line();
}

// 保存的光标列号
static int saved_x = 0;

// 保存的光标行号
static int saved_y = 0;

// 保存当前光标位置
static void save_cur(void)
{
	saved_x = x;
	saved_y = y;
}

// 恢复保存的光标位置
static void restore_cur(void)
{
	gotoxy(saved_x, saved_y);
}

// 控制台写函数
// 从终端对应的 tty 写缓冲队列中取字符，并显示在屏幕上
void con_write(struct tty_struct *tty)
{
	int nr;
	char c;

	// 首先取得写缓冲队列中现有字符数 nr，然后针对每个字符进行处理
	nr = CHARS(tty->write_q);
	while (nr--)
	{
		// 从写队列中取一字符 c，根据前面所处理字符的状态 state 分别处理
		// 状态之间的转换关系为：
		// state = 0：初始状态；或者原是状态4；或者原是状态1，但字符不是'['；
		// 1：原是状态0，并且字符是转义字符ESC(0x1b = 033 = 27)；
		// 2：原是状态1，并且字符是'['；
		// 3：原是状态2；或者原是状态3，并且字符是';'或数字；
		// 4：原是状态3，并且字符不是';'或数字；
		GETCH(tty->write_q, c);
		switch (state)
		{
		case 0:
			// 如果字符不是控制字符(c>31)，并且也不是扩展字符(c<127)
			if (c > 31 && c < 127)
			{
				// 若当前光标处在行末端或末端以外，则将光标移到下行头列
				// 并调整光标位置对应的内存指针 pos
				if (x >= video_num_columns)
				{
					x -= video_num_columns;
					pos -= video_size_row;
					lf();
				}

				// 将字符 c 写到显示内存中 pos 处，并将光标右移 1 列
				// 同时也将 pos 对应地移动 2 个字节
				__asm__("movb attr,%%ah\n\t"
						"movw %%ax,%1\n\t" ::"a"(c),
						"m"(*(short *)pos));
				pos += 2;
				x++;
			}
			// 如果字符 c 是转义字符 ESC，则转换状态state 到 1
			else if (c == 27)
				state = 1;

			// 如果字符 c 是换行符(10)，或是垂直制表符VT(11)，或者是换页符FF(12)，则移动光标到下一行
			else if (c == 10 || c == 11 || c == 12)
				lf();

			// 如果字符 c 是回车符 CR(13)，则将光标移动到头列(0 列)
			else if (c == 13)
				cr();

			// 如果字符 c 是 DEL(127)，则将光标右边一字符擦除(用空格字符替代)，并将光标移到被擦除位置
			else if (c == ERASE_CHAR(tty))
				del();

			// 如果字符 c 是BS(backspace,8)，则将光标右移 1 格
			// 并相应调整光标对应内存位置指针 pos
			else if (c == 8)
			{
				if (x)
				{
					x--;
					pos -= 2;
				}
			}

			// 如果字符 c 是水平制表符TAB(9)，则将光标移到 8 的倍数列上
			// 若此时光标列数超出屏幕最大列数，则将光标移到下一行上
			else if (c == 9)
			{
				c = 8 - (x & 7);
				x += c;
				pos += c << 1;
				if (x > video_num_columns)
				{
					x -= video_num_columns;
					pos -= video_size_row;
					lf();
				}
				c = 9;
			}
			// 如果字符 c 是响铃符 BEL(7)，则调用蜂鸣函数，是扬声器发声
			else if (c == 7)
				sysbeep();
			break;
		// 如果原状态是 0，并且字符是转义字符 ESC(0x1b = 033 = 27)，则转到状态 1 处理
		case 1:
			state = 0;

			// 如果字符 c 是 '['，则将状态 state 转到 2
			if (c == '[')
				state = 2;

			// 如果字符 c 是 'E'，则光标移到下一行开始处(0 列)
			else if (c == 'E')
				gotoxy(0, y + 1);

			// 如果字符 c 是 'M'，则光标上移一行
			else if (c == 'M')
				ri();

			// 如果字符 c 是 'D'，则光标下移一行
			else if (c == 'D')
				lf();

			// 如果字符 c 是'Z'，则发送终端应答字符序列
			else if (c == 'Z')
				respond(tty);

			// 如果字符 c 是 '7'，则保存当前光标位置
			// 注意这里代码写错！应该是(c == '7')
			else if (x == '7')
				save_cur();

			// 如果字符 c 是'8'，则恢复到原保存的光标位置
			// 注意这里代码写错！应该是(c=='8')
			else if (x == '8')
				restore_cur();
			break;

			// 如果原状态是 1，并且上一字符是 '['，则转到状态 2 来处理
		case 2:
			// 首先对 ESC 转义字符序列参数使用的处理数组 par[] 清零
			// 索引变量 npar 指向首项，并且设置状态为 3
			// 若此时字符不是'?'，则直接转到状态 3 去处理
			// 否则去读一字符，再到状态 3 处理代码处
			for (npar = 0; npar < NPAR; npar++)
				par[npar] = 0;
			npar = 0;
			state = 3;
			if ((ques = (c == '?')))
				break;

		// 如果原来是状态 2；或者原来就是状态 3，但原字符是';'或数字，则在下面处理
		case 3:
			// 如果字符 c 是分号 ';'，并且数组 par 未满，则索引值加 1
			if (c == ';' && npar < NPAR - 1)
			{
				npar++;
				break;
			}

			// 如果字符 c 是数字字符 '0'-'9'
			// 则将该字符转换成数值并与 npar 所索引的项组成 10 进制数
			else if (c >= '0' && c <= '9')
			{
				par[npar] = 10 * par[npar] + c - '0';
				break;
			}
			else
				// 否则转到状态 4
				state = 4;
		// 如果原状态是状态 3，并且字符不是';'或数字，则转到状态 4 处理。首先复位状态 state=0
		case 4:
			state = 0;
			switch (c)
			{

			// 如果字符 c 是 'G'或'`'，则 par[] 中第一个参数代表列号
			// 若列号不为零，则将光标右移一格
			case 'G':
			case '`':
				if (par[0])
					par[0]--;
				gotoxy(par[0], y);
				break;

			// 如果字符 c 是 'A'，则第一个参数代表光标上移的行数
			// 若参数为 0 则上移一行
			case 'A':
				if (!par[0])
					par[0]++;
				gotoxy(x, y - par[0]);
				break;

			// 如果字符 c 是'B'或'e'，则第一个参数代表光标下移的行数
			// 若参数为 0 则下移一行
			case 'B':
			case 'e':
				if (!par[0])
					par[0]++;
				gotoxy(x, y + par[0]);
				break;

			// 如果字符 c 是'C'或'a'，则第一个参数代表光标右移的格数
			// 若参数为 0 则右移一格
			case 'C':
			case 'a':
				if (!par[0])
					par[0]++;
				gotoxy(x + par[0], y);
				break;

			// 如果字符 c 是'D'，则第一个参数代表光标左移的格数
			// 若参数为 0 则左移一格
			case 'D':
				if (!par[0])
					par[0]++;
				gotoxy(x - par[0], y);
				break;

			// 如果字符 c 是'E'，则第一个参数代表光标向下移动的行数
			// 并回到 0 列；若参数为 0 则下移一行
			case 'E':
				if (!par[0])
					par[0]++;
				gotoxy(0, y + par[0]);
				break;

			// 如果字符 c 是'F'，则第一个参数代表光标向上移动的行数
			// 并回到 0 列；若参数为 0 则上移一行
			case 'F':
				if (!par[0])
					par[0]++;
				gotoxy(0, y - par[0]);
				break;

			// 如果字符 c 是'd'，则第一个参数代表光标所需在的行号(从 0 计数)
			case 'd':
				if (par[0])
					par[0]--;
				gotoxy(x, par[0]);
				break;

			// 如果字符 c 是'H'或'f'
			// 则第一个参数代表光标移到的行号
			// 第二个参数代表光标移到的列号
			case 'H':
			case 'f':
				if (par[0])
					par[0]--;
				if (par[1])
					par[1]--;
				gotoxy(par[1], par[0]);
				break;

			// 如果字符 c 是'J'，则第一个参数代表以光标所处位置清屏的方式：
			// ANSI 转义序列：'ESC [sJ'(s = 0 删除光标到屏幕底端；1 删除屏幕开始到光标处；2 整屏删除)
			case 'J':
				csi_J(par[0]);
				break;

			// 如果字符 c 是'K'，则第一个参数代表以光标所在位置对行中字符进行删除处理的方式
			// ANSI 转义字符序列：'ESC [sK'(s = 0 删除到行尾；1 从开始删除；2 整行都删除)
			case 'K':
				csi_K(par[0]);
				break;

			// 如果字符 c 是'L'，表示在光标位置处插入 n 行(ANSI 转义字符序列'ESC [nL')
			case 'L':
				csi_L(par[0]);
				break;

			// 如果字符 c 是'M'，表示在光标位置处删除 n 行(ANSI 转义字符序列'ESC [nM')
			case 'M':
				csi_M(par[0]);
				break;

			// 如果字符 c 是'P'，表示在光标位置处删除 n 个字符(ANSI 转义字符序列'ESC [nP')
			case 'P':
				csi_P(par[0]);
				break;

			// 如果字符 c 是'@'，表示在光标位置处插入 n 个字符(ANSI 转义字符序列'ESC [n@')
			case '@':
				csi_at(par[0]);
				break;

			// 如果字符 c 是'm'，表示改变光标处字符的显示属性，比如加粗、加下划线、闪烁、反显等
			// ANSI 转义字符序列：'ESC [nm'。n = 0 正常显示；1 加粗；4 加下划线；7 反显；27 正常显示；
			case 'm':
				csi_m();
				break;

			// 如果字符 c 是'r'，则表示用两个参数设置滚屏的起始行号和终止行号
			case 'r':
				if (par[0])
					par[0]--;
				if (!par[1])
					par[1] = video_num_lines;
				if (par[0] < par[1] &&
					par[1] <= video_num_lines)
				{
					top = par[0];
					bottom = par[1];
				}
				break;

			// 如果字符 c 是's'，则表示保存当前光标所在位置
			case 's':
				save_cur();
				break;

			// 如果字符 c 是'u'，则表示恢复光标到原保存的位置处
			case 'u':
				restore_cur();
				break;
			}
		}
	}

	// 最后根据上面设置的光标位置，向显示控制器发送光标显示位置
	set_cursor();
}

// void con_init(void);
// 这个子程序初始化控制台中断，其它什么都不做
// 如果你想让屏幕干净的话，就使用适当的转义字符序列调用 tty_write() 函数

// 读取 setup.s 程序保存的信息，用以确定当前显示器类型，并且设置所有相关参数
void con_init(void)
{
	register unsigned char a;
	char *display_desc = "????";
	char *display_ptr;

	// 显示器显示字符列数
	video_num_columns = ORIG_VIDEO_COLS;

	// 每行需使用字节数
	video_size_row = video_num_columns * 2;

	// 显示器显示字符行数
	video_num_lines = ORIG_VIDEO_LINES;

	// 当前显示页面
	video_page = ORIG_VIDEO_PAGE;

	// 擦除字符(0x20 显示字符， 0x07 是属性)
	video_erase_char = 0x0720;

	// 如果原始显示模式等于 7，则表示是单色显示器
	if (ORIG_VIDEO_MODE == 7) /* Is this a monochrome display? */
	{
		// 设置单显映象内存起始地址
		video_mem_start = 0xb0000;

		// 设置单显索引寄存器端口
		video_port_reg = 0x3b4;

		// 设置单显数据寄存器端口
		video_port_val = 0x3b5;

		// 根据 BIOS 中断 int 0x10 功能 0x12 获得的显示模式信息
		// 判断显示卡单色显示卡还是彩色显示卡
		// 如果使用上述中断功能所得到的 BX 寄存器返回值不等于 0x10，则说明是 EGA 卡
		// 因此初始显示类型为 EGA 单色；所使用映象内存末端地址为 0xb8000
		// 并置显示器描述字符串为'EGAm'
		// 在系统初始化期间显示器描述字符串将显示在屏幕的右上角
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			// 设置显示类型(EGA 单色)
			video_type = VIDEO_TYPE_EGAM;

			// 设置显示内存末端地址
			video_mem_end = 0xb8000;

			// 设置显示描述字符串
			display_desc = "EGAm";
		}

		// 如果 BX 寄存器的值等于 0x10，则说明是单色显示卡 MDA。则设置相应参数
		else
		{
			// 设置显示类型(MDA 单色)
			video_type = VIDEO_TYPE_MDA;

			// 设置显示内存末端地址
			video_mem_end = 0xb2000;

			// 设置显示描述字符串
			display_desc = "*MDA";
		}
	}

	// 如果显示模式不为 7，则为彩色模式
	// 此时所用的显示内存起始地址为 0xb800
	// 显示控制索引寄存器端口地址为 0x3d4；
	// 数据寄存器端口地址为 0x3d5
	else /* If not, it is color. */
	{
		// 显示内存起始地址
		video_mem_start = 0xb8000;

		// 设置彩色显示索引寄存器端口
		video_port_reg = 0x3d4;

		// 设置彩色显示数据寄存器端口
		video_port_val = 0x3d5;

		// 再判断显示卡类别，如果 BX 不等于 0x10，则说明是 EGA 显示卡
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			// 设置显示类型(EGA 彩色)
			video_type = VIDEO_TYPE_EGAC;

			// 设置显示内存末端地址
			video_mem_end = 0xbc000;

			// 设置显示描述字符串
			display_desc = "EGAc";
		}

		// 如果 BX 寄存器的值等于 0x10，则说明是 CGA 显示卡，则设置相应参数
		else
		{
			// 设置显示类型(CGA)
			video_type = VIDEO_TYPE_CGA;

			// 设置显示内存末端地址
			video_mem_end = 0xba000;

			// 设置显示描述字符串
			display_desc = "*CGA";
		}
	}

	// 让用户知道我们正在使用哪一类显示驱动程序

	// 在屏幕的右上角显示显示描述字符串
	// 采用的方法是直接将字符串写到显示内存的相应位置处
	// 首先将显示指针 display_ptr 指到屏幕第一行右端差 4 个字符处(每个字符需 2 个字节，因此减8)
	display_ptr = ((char *)video_mem_start) + video_size_row - 8;
	while (*display_desc)
	{
		// 复制字符
		*display_ptr++ = *display_desc++;

		// 空开属性字节位置
		display_ptr++;
	}

	// 初始化用于滚屏的变量(主要用于EGA/VGA)

	// 滚屏起始显示内存地址
	origin = video_mem_start;

	// 滚屏结束内存地址
	scr_end = video_mem_start + video_num_lines * video_size_row;

	// 最顶行号
	top = 0;

	// 最底行号
	bottom = video_num_lines;

	// 初始化光标位置 x, y 和对应的内存位置 pos
	gotoxy(ORIG_X, ORIG_Y);

	// 设置键盘中断陷阱门
	set_trap_gate(0x21, &keyboard_interrupt);

	// 取消 8259A 中对键盘中断的屏蔽，允许 IRQ1
	outb_p(inb_p(0x21) & 0xfd, 0x21);

	// 延迟读取键盘端口 0x61(8255A 端口 PB)
	a = inb_p(0x61);

	// 设置禁止键盘工作(位 7 置位)
	outb_p(a | 0x80, 0x61);

	// 再允许键盘工作，用以复位键盘操作
	outb(a, 0x61);
}
/* from bsd-net-2: */

// 停止蜂鸣
// 复位 8255A PB 端口的位 1 和位 0
void sysbeepstop(void)
{
	// 禁止定时器 2
	outb(inb_p(0x61) & 0xFC, 0x61);
}

int beepcount = 0;

// 开通蜂鸣
// 8255A 芯片 PB 端口的位 1 用作扬声器的开门信号；
// 位 0 用作 8253 定时器 2 的门信号
// 该定时器的输出脉冲送往扬声器，作为扬声器发声的频率
// 因此要使扬声器蜂鸣，需要两步：
// 首先开启PB 端口位 1 和位 0（置位）
// 然后设置定时器发送一定的定时频率即可
static void sysbeep(void)
{
	// 开启定时器 2
	outb_p(inb_p(0x61) | 3, 0x61);

	// 送设置定时器 2 命令
	outb_p(0xB6, 0x43);

	// 设置频率为 750HZ，因此送定时值 0x637
	outb_p(0x37, 0x42);
	outb(0x06, 0x42);

	// 蜂鸣时间为 1/8 秒
	beepcount = HZ / 8;
}
