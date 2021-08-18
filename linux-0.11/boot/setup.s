!
!	setup.s		(C) 1991 Linus Torvalds
!

! setup.s 负责从BIOS 中获取系统数据，并将这些数据放到系统内存的适当地方
! 此时 setup.s 和 system 已经由 bootsect 引导块加载到内存中
!
! 这段代码询问 bios 有关内存/磁盘/其它参数，并将这些参数放到一个
! “安全的”地方：0x90000-0x901FF，也即原来 bootsect 代码块曾经在
! 的地方，然后在被缓冲块覆盖掉之前由保护模式的 system 读取

! 注意！以下这些参数最好和 bootsect.s 中的相同！

! 原来bootsect 所处的段
INITSEG  = 0x9000

! system 在 0x10000(64k) 处。
SYSSEG   = 0x1000

! 本程序所在的段地址
SETUPSEG = 0x9020

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

entry start
start:

! bootsect 中 jmpi	0,SETUPSEG 将会跳转到这个地方
! ok，整个读磁盘过程都正常，现在将光标位置保存以备今后使用。

	! 将 ds 置成 #INITSEG(0x9000)。这已经在 bootsect 程序中设置过
	! 但是现在是 setup 程序，为了确保没问题，Linus 觉得需要再重新设置一下。
	mov	ax,#INITSEG
	mov	ds,ax

	! 获取光标位置, 
	! 返回值 (DH, DL) 对应 (行, 列)
	mov	ah,#0x03
	xor	bh,bh
	int	0x10

	! 将光标位置存储到 0x90000 处
	mov	[0],dx

	! 获取内存大小
	! 是调用中断0x15，功能号ah = 0x88
	! 返回：ax = 从 0x100000（1M）处开始的扩展内存大小(KB)。
	! 若出错则 CF 置位，ax = 出错码
	mov	ah,#0x88
	int	0x15

	! 将扩展内存数值存在 0x90002 处（1 个字）
	mov	[2],ax 

	! 下面这段用于取显示卡当前显示模式。
	! 调用BIOS 中断 0x10
	! 功能号 ah = 0x0f
	! 返回：ah = 字符列数，al = 显示模式，bh = 当前显示页。

	mov	ah,#0x0f
	int	0x10

	! 0x90004 (1 字)存放当前页
	mov	[4],bx

	! 0x90006 显示模式，0x90007 字符列数。
	mov	[6],ax

	! 检查显示方式（EGA/VGA）并取参数。
	! 调用BIOS 中断 0x10，附加功能选择 - 取方式信息
	! 功能号：ah = 0x12，bl = 0x10
	! 返回：bh = 显示状态
	! (0x00 - 彩色模式，I/O 端口=0x3dX)
	! (0x01 - 单色模式，I/O 端口=0x3bX)
	! bl = 安装的显示内存
	! (0x00 - 64k, 0x01 - 128k, 0x02 - 192k, 0x03 = 256k)
	! cx = 显示卡特性参数。

	! 表示附加功能选择
	mov	ah,#0x12

	! 用于返回显示器状态
	mov	bl,#0x10
	int	0x10

	! 0x90008 = AX; 返回值 (AL) = 12H - 表示该功能支持
	mov	[8],ax

	! 0x9000A = 安装的显示内存，0x9000B = 显示状态(彩色/单色)
	mov	[10],bx

	! 0x9000C = 显示卡特性参数
	mov	[12],cx

	! 取第一个硬盘的信息（复制硬盘参数表）
	! 第 1 个硬盘参数表的首地址竟然是中断向量 0x41 的向量值
	! 第 2 个硬盘参数表紧接第 1 个表的后面，
	! 中断向量 0x46 的向量值也指向这第 2 个硬盘的参数表首址
	! 表的长度是 16 个字节(0x10)。
	! 下面两段程序分别复制 BIOS 有关两个硬盘的参数表
	! 0x90080 处存放第 1 个硬盘的参数表
	! 0x90090 处存放第 2 个硬盘的参数表

	mov	ax,#0x0000
	mov	ds,ax

	! 取中断向量 0x41 的值，也即 hd0 参数表的地址 -> ds:si
	lds	si,[4*0x41]
	mov	ax,#INITSEG

	! 传输的目的地址: 0x9000:0x0080 -> es:di
	mov	es,ax
	mov	di,#0x0080

	! 共传输 0x10 字节
	mov	cx,#0x10
	rep
	movsb

	! 获取第二个硬盘的信息

	mov	ax,#0x0000
	mov	ds,ax

	! 取中断向量 0x46 的值，也即 hd1 参数表的地址 -> ds:si
	lds	si,[4*0x46]
	mov	ax,#INITSEG

	! 传输的目的地址: 0x9000:0x0090 -> es:di
	mov	es,ax
	mov	di,#0x0090

	! 共传输 0x10 字节
	mov	cx,#0x10
	rep
	movsb

	! 检查系统是否存在第 2 个硬盘
	! 如果不存在则第 2 个硬盘参数表清零
	! 利用BIOS 中断调用 0x13 的取盘类型功能
	! 功能号 ah = 0x15；
	! 输入：dl = 驱动器号（0x8X 是硬盘：0x80 指第 1 个硬盘，0x81 第 2 个硬盘）
	! 输出：
	! ah = 类型码；
	! 00 --没有这个盘，CF 置位
	! 01 -- 是软驱，没有 change-line 支持
	! 02 -- 是软驱(或其它可移动设备)，有 change-line 支持
	! 03 -- 是硬盘

	mov	ax,#0x01500
	mov	dl,#0x81
	int	0x13

	jc	no_disk1

	! 是硬盘吗？(类型 = 3?)
	cmp	ah,#3 

	je	is_disk1
no_disk1:
	! 第 2 个硬盘不存在，则对第 2 个硬盘表清零。
	mov	ax,#INITSEG
	mov	es,ax
	mov	di,#0x0090
	mov	cx,#0x10
	mov	ax,#0x00
	rep
	stosb

is_disk1:

! 现在我们要开始进入保护模式了...

	! 关中断
	cli

	! 我们首先将 system 挪到正确的位置

	! bootsect 引导程序是将 system 模块读入到从 0x10000（64k）开始的位置
	! 由于当时假设 system 模块最大长度不会超过 0x80000（512k），也即其末端不会超过内存地址0x90000
	! 所以 bootsect 会将自己移动到 0x90000 开始的地方，并把 setup 加载到它的后面
	! 下面这段程序的用途是再把整个 system 模块移动到 0x00000 位置
	! 即把从 0x10000 到 0x8ffff 的内存数据块(512k)
	! 整体向内存低端移动了0x10000（64k）的位置。

	mov	ax,#0x0000

	! 清空方向标志，表示从低向高拷贝
	cld

do_move:
	! es:di 表示目标地址
	! ds:si 表示源地址

	! 设置源段寄存器
	mov	es,ax

	! 将段加 0x1000，用于设置源段寄存器
	add	ax,#0x1000

	! 如果段寄存器等于 0x9000，说明已经完成移动
	cmp	ax,#0x9000

	! 跳转到结束移动
	jz	end_move

	! 设置源段寄存器
	mov	ds,ax		! source segment

	! di = 0
	sub	di,di

	! si = 0
	sub	si,si

	! 移动 0x8000 个字 (64KB)
	mov 	cx,#0x8000

	! 重复执行 movsw cx 次
	rep
	movsw

	! 继续下一次 64K 的移动
	jmp	do_move

! 数据移动完成，也就是说完成了 system 的重定位
! 这个依赖于 -Ttext 0 编译选项，表示代码在 0 开始的位置
! 然后我们加载段描述符。

end_move:
	! ds 指向 setup 段
	mov	ax,#SETUPSEG	! right, forgot this at first. didn't work :-)
	mov	ds,ax

	! 加载 idt
	lidt	idt_48

	! 加载 gdt 全局描述符表
	lgdt	gdt_48

	! 以上操作很简单，现在我们开启 A20 线

	! 等待输入缓冲区清空
	! 只有当输入缓冲器为空时才可以对其进行写命令
	call	empty_8042

	! 0xD1 命令码-表示要写数据到 8042 的 P2 端口
	! P2 端口的位 1 用于 A20 线的选通
	mov	al,#0xD1		! command write
	out	#0x64,al

	! 等待输入缓冲区清空
	call	empty_8042

	! 数据要写到 0x60 端口
	mov	al,#0xDF		! A20 on
	out	#0x60,al

	! 等待输入缓冲区清空
	call	empty_8042

	! 希望以上一切正常。现在我们必须重新对中断进行编程 😔
	! 我们将它们放在正好处于 intel 保留的硬件中断后面，在int 0x20-0x2F
	! 在那里它们不会引起冲突。不幸的是 IBM 在原 PC 机中搞糟了，以后也没有纠正过来
	! PC 机的 BIOS 将中断放在了 0x08-0x0f，这些中断也被用于内部硬件中断
	! 所以我们就必须重新对 8259 中断控制器进行编程，这一点都没劲

	! 0x11 表示初始化命令开始，是 ICW1 命令字
	! 表示 边沿触发、多片8259 级连、最后要发送 ICW4 命令字
	mov	al,#0x11

	! 发送到 8259A 主芯片
	out	#0x20,al		! send it to 8259A-1

	! 这样代码用于延迟，等待输入输出完成，相当于 jmp $+2, jmp $+2
	.word	0x00eb,0x00eb		! jmp $+2, jmp $+2

	! 发送到 8259A 从芯片
	out	#0xA0,al		! and to 8259A-2

	! 延迟
	.word	0x00eb,0x00eb

	! 设置主芯片起始中断号 0x20
	mov	al,#0x20

	! 送主芯片 ICW2 命令字
	out	#0x21,al
	
	! 延迟
	.word	0x00eb,0x00eb

	! 设置从芯片起始中断号 0x28
	mov	al,#0x28

	! 送从芯片 ICW2 命令字
	out	#0xA1,al

	! 延迟
	.word	0x00eb,0x00eb

	! 送主芯片 ICW3 命令字，主芯片的 IR2 连从芯片 INT
	mov	al,#0x04
	out	#0x21,al

	! 延迟
	.word	0x00eb,0x00eb

	! 送从芯片 ICW3 命令字，表示从芯片的 INT 连到主芯片的 IR2 引脚上
	mov	al,#0x02
	out	#0xA1,al

	! 延迟
	.word	0x00eb,0x00eb

	! 送主芯片 ICW4 命令字。8086 模式；普通 EOI 方式，
	! 需发送指令来复位 初始化结束，芯片就绪
	mov	al,#0x01		! 8086 mode for both
	out	#0x21,al

	! 延迟
	.word	0x00eb,0x00eb

	! 送从芯片 ICW4 命令字，内容同上
	out	#0xA1,al

	! 延迟
	.word	0x00eb,0x00eb

	! 屏蔽主芯片所有中断请求
	mov	al,#0xFF
	out	#0x21,al

	! 延迟
	.word	0x00eb,0x00eb

	！屏蔽从芯片所有中断请求
	out	#0xA1,al

! 唉，上面这段相当没劲 😔，希望这样能工作，而且我们也不再需要乏味的 BIOS 了
! （除了初始的加载 😊。BIOS 子程序要求很多不必要的数据，而且它一点都没趣。
! 那是“真正”的程序员所做的事
! https://wiki.c2.com/?RealProgrammer
! REAL programmer 指的是，粗略地说是使用二进制编码的程序员 ……

! 好的 👌，现在我们就要真正进入保护模式了
! 为了使得事情尽可能简单，我们不会做任何的初始化工作
! 我们在 GNU 编译的 32 位程序里干这些事情
! 我们只需要在保护模式跳转到 0x00000 的位置就好了

	! 下面两行设置 CR0 寄存器的 PE（Protect Enable）保护模式有效位
	mov	ax,#0x0001	! 

	! 用于将 ax 寄存器设置到 cr0 寄存器的低 16 位
	! 使用这个指令的主要动机是为了兼容 206 机器
	! 对于 206 后的 cpu 可以直接使用 mov cr0, eax 的这种方式来更好的处理
	lmsw	ax

	! 相当于 jmp 8:0
	! 其中 8 = 0b_1000，是代码段选择子，总共长度 16 位
	! 其中 0 ~ 1 位表示 RPL(请求特权级)，
	! 第 2 位表示 TI = 0 表示在 GDT 中，(TI = 1 表示在 LDT 中，这里用不到)
	! 然后后面的 13 位表示 GDT 表（数组）中的索引

	jmpi	0,8

! 下面这个子程序检查键盘命令队列是否为空。这里不使用超时方法
! 如果这里死机，则说明 PC 机有问题，我们就没有办法再处理下去了
! 只有当输入缓冲器为空时（状态寄存器位 2 = 0）才可以对其进行写命令
empty_8042:
	! 用于延迟
	.word	0x00eb,0x00eb

	! 读 AT 键盘控制器状态寄存器
	in	al,#0x64	! 8042 status port

	! 测试位 2，输入缓冲器满？
	test	al,#2

	! 是，则死循环
	jnz	empty_8042	! yes - loop

	! 否则返回
	ret

! 全局描述符表开始处。描述符表由多个 8 字节长的描述符项组成
gdt:
	! 空描述符，这是 Intel 规定的一项
	.word	0,0,0,0		! dummy

	! 代码段
	! 段界限 8MB - 1
	.word	0x07FF

	! 基地址 0x000000
	.word	0x0000

	! 代码段，可读/可执行
	.word	0x9A00

	! 粒度为 4K，32 位代码
	.word	0x00C0

	! 数据段
	! 段界限 8MB - 1
	.word	0x07FF

	! 基地址 0x000000
	.word	0x0000

	! 数据段，可读/可写
	.word	0x9200

	! 粒度为 4K，32 位数据
	.word	0x00C0


idt_48:
	! IDT 的指针，用于设置 idtr 寄存器
	! 这里基地址和界限都为 0，表示没有任何中断描述符
	.word	0			! idt 界限
	.word	0,0			! idt 基地址

gdt_48:
	! GDT 的指针，用于设置 gdtr 寄存器
	! 按理说这里应该是界限 = 长度 - 1，也就是 0x800 - 1 = 0x7FF，但是好像也能跑
	.word	0x800		! gdt 界限 表示有 0x100 = 256 个描述符
	.word	512+gdt,0x9	! gdt 基地址 = 0X9xxxx
	
.text
endtext:
.data
enddata:
.bss
endbss:
