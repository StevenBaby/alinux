!
!	bootsect.s		(C) 1991 Linus Torvalds
!
! 这段代码比我岁数都大.....

! SYS_SIZE 是要加载的节数 (16 字节为1 节) 
! 0x3000 共为 0x30000 字节=196 kB (若以1024 字节为 1KB 计，则应该是192KB) 
! 对于当前的版本空间已足够了。

SYSSIZE = 0x3000

! bootsect.s 被 bios-启动子程序加载至 0x7c00 (31k) 处，
! 并将自己移到了地址 0x90000 (576k) 处，并跳转至那里。
!
! 它然后使用 BIOS 中断将 'setup' 直接加载到自己的后面 (0x90200)(576.5k)
! 并将 system 加载到地址 0x10000 处
!
! 注意! 目前的内核系统最大长度限制为 (8*65536)(512k) 字节，即使是在
! 将来这也应该没有问题的。我想让它保持简单明了
! 这样 512K 的最大内核长度应该足够了
! 尤其是这里没有像 minix 中一样包含缓冲区高速缓冲
!
! 加载程序已经做的够简单了，所以持续的读出错将导致死循环。只能手工重启
! 只要可能，通过一次读取所有的扇区，加载过程可以做的很快。

.globl begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
.data
begdata:
.bss
begbss:
.text

SETUPLEN = 4				! setup 程序的扇区数 (setup-sectors) 值；
BOOTSEG  = 0x07c0			! bootsect 的原始地址 (是段地址，以下同) ；
INITSEG  = 0x9000			! 将 bootsect 移到这里，避开 0x7c00 这个位置；
SETUPSEG = 0x9020			! setup 程序从这里开始；
SYSSEG   = 0x1000			! system 模块加载到 0x10000 (64 kB) 处；
ENDSEG   = SYSSEG + SYSSIZE	! 停止加载的段地址；

! ROOT_DEV:	0x000 - 根文件系统设备使用与引导时同样的软驱设备
!		0x301 - 根文件系统设备在第一个硬盘的第一个分区上，等等；

ROOT_DEV = 0x306

! 指定根文件系统设备是第 2 个硬盘的第 1 个分区。这是 Linux 老式的硬盘命名方式
! 具体值的含义如下：
! 设备号 = 主设备号*256 + 次设备号 (也即 dev_no = (major << 8) + minor)
! (主设备号：1-内存, 2-磁盘, 3-硬盘, 4-ttyx, 5-tty, 6-并行口, 7-非命名管道)
! 0x300 - /dev/hd0 - 代表整个第 1 个硬盘；
! 0x301 - /dev/hd1 - 第 1 个盘的第 1 个分区；
! …
! 0x304 - /dev/hd4 - 第 1 个盘的第 4 个分区；
! 0x305 - /dev/hd5 - 代表整个第 2 个硬盘；
! 0x306 - /dev/hd6 - 第 2 个盘的第 1 个分区；
! …
! 0x309 - /dev/hd9 - 第 2 个盘的第 4 个分区；
! 从linux 内核 0.95 版后已经使用与现在相同的命名方法了

entry _start
_start: 
	! 主引导扇区从这里开始 (下面将被加载到 0x7c00) 的位置
	! 作用是将自身 (bootsect) 从目前段位置 0x07c0(31k)
	! 移动到 0x9000(576k) 处，共 256 字 (512 字节)
	! 然后跳转到移动后代码的 go 标号处，也即本程序的下一语句处。

	! 将ds 段寄存器置为0x7C0
	mov	ax,#BOOTSEG 
	mov	ds,ax

	! 将es 段寄存器置为0x9000
	mov	ax,#INITSEG
	mov	es,ax

	! 总共复制 256 字
	mov	cx,#256

	! 将 si 和 di 清零
	! 源地址 ds:si = 0x07C0:0x0000
	sub	si,si

	! 目的地址 es:di = 0x9000:0x0000
	sub	di,di

	! 重复执行 movw 指令 cx 次，也就是 256 次
	! 该指令执行完成后，si 和 di 会自动 +2
	rep
	movw ! 移动一个字

	! 段间跳转 相当于 nasm 语法 jmp INITSEG:go
	! 跳转之前，这行指令的地址是 0x7c15
	jmpi	go,INITSEG

go:	
	! 上面段间跳转就会跳到这个地方
	! 执行过程中，这个地方的地址是 0x9001a

	! 将 ds, es 和 ss 都设置成移动后代码所在的段处 (0x9000)
	mov	ax,cs
	mov	ds,ax
	mov	es,ax

	! 将堆栈指针 sp 指向 0x9ff00 (即0x9000:0xff00) 处
	mov	ss,ax

	! 任意远远大于 0x200 的数，确保程序执行过程中，栈不会溢出
	mov	sp,#0xFF00

	! 由于代码段移动过了，所以要重新设置堆栈段的位置。
	! sp 只要指向远大于512 偏移 (即地址0x90200) 处
	! 都可以。因为从0x90200 地址开始处还要放置setup 程序，
	! 而此时setup 程序大约为4 个扇区，因此sp 要指向大
	! 于 (0x200 + 0x200 * 4 + 堆栈大小) 处。

! 在 bootsect 程序块后紧根着加载 setup 模块的代码数据。
! 注意 es 已经设置好了。 (在移动代码时 es 已经指向目的段地址处 0x9000) 。

load_setup:
	! 这段代码的用途是利用 BIOS 中断 INT 0x13 
	! 将setup 模块从磁盘第 2 个扇区开始读到 0x90200 开始处，共读 4 个扇区
	! 如果读出错，则复位驱动器，并重试，没有退路。INT 0x13 的使用方法如下

	! 读扇区，调用的参数
	! ah = 0x02 - 表示读磁盘扇区到内存；
	! al = 需要读出的扇区数量；
	! ch = 磁道(柱面)号的低 8 位； 
	! cl = 开始扇区(0-5 位)，磁道号高 2 位(6-7)；
	! dh = 磁头号； 
	! dl = 驱动器号 (如果是硬盘则位 7 要置位) ；
	! es:bx -> 指向数据缓冲区；
	! 返回值
	! 将数据读取到 es:bx 指向的位置；
	! 如果出错则 CF 标志置位；

	! 驱动器 0，磁头号 0
	mov	dx,#0x0000

	! 扇区号 2，磁道号 0
	mov	cx,#0x0002

	! 读物保存的位置为 INITSEG:0x200
	mov	bx,#0x0200

	! ah = 0x02 - 表示读磁盘扇区到内存；
	! al = SETUPLEN 表示需要读取的扇区数量 
	mov	ax,#0x0200+SETUPLEN

	! 读磁盘
	int	0x13

	! 如果读取成功则跳转到 ok_load_setup
	jnc	ok_load_setup

	! 否则，重置磁盘
	mov	dx,#0x0000
	mov	ax,#0x0000		! reset the diskette
	int	0x13

	! 跳转到 load_setup，重试
	j	load_setup

ok_load_setup:

	! 获取磁盘驱动器的参数，特别是每道的扇区数量。
	! 取磁盘驱动器参数 INT 0x13 调用格式和返回信息如下：
	! ah = 0x08 
	! dl = 驱动器号 (如果是硬盘则要置位 7 为 1) 。
	! 返回信息：
	! 如果出错则 CF 置位，并且 ah = 状态码。
	! ah = 0，
	! al = 0，
	! bl = 驱动器类型 (AT/PS2) 
	! ch = 最大磁道号的低 8 位
	! cl = 每磁道最大扇区数(位 0-5)
	! 最大磁道号高 2 位 (位 6-7)
	! dh = 最大磁头数
	! dl = 驱动器数量，
	! es:di -> 软驱磁盘参数表。

	mov	dl,#0x00

	! AH=8 表示获取驱动器参数
	mov	ax,#0x0800
	int	0x13

	mov	ch,#0x00

	! 表示下一条语句的操作数在 cs 段寄存器所指的段中。
	seg cs

	! 保存每磁道的扇区数，加上上一条指令 nasm 语法表示 mov cs:[sectors], cx
	mov	sectors,cx

	! 因为上面取磁盘参数中断改掉了 es 的值，这里重新改回来
	mov	ax,#INITSEG
	mov	es,ax

	! 显示一些信息('Loading system ...'回车换行，共24 个字符)。

	mov	ah,#0x03		! 获取光标位置
	xor	bh,bh
	int	0x10

	! 总共 24 个字符
	mov	cx,#24

	! 第 0 页，默认样式
	mov	bx,#0x0007

	! bp 指向要显示的字符串
	mov	bp,#msg1

	! 写字符并移动光标
	mov	ax,#0x1301
	int	0x10

	! 现在我们已经显示完了消息
	! 然后开始将 system 模块加载到 0x10000(64k) 处

	! 将 es 赋值为 0x1000
	mov	ax,#SYSSEG
	mov	es,ax		! segment of 0x010000

	! 读取 system 模块，es 为参数
	call	read_it

	! 关闭驱动器马达
	call	kill_motor

	! 然后，我们检查要使用哪个根文件系统设备 (简称根设备) 
	! 如果已经指定了设备(!=0)，就直接使用给定的设备
	! 否则就需要根据BIOS 报告的每磁道扇区数来
	! 确定到底使用 /dev/PS0 (2,28) 还是 /dev/at0 (2,8)。

	! 上面一行中两个设备文件的含义：
	! 在 Linux 中软驱的主设备号是 2
	! 次设备号 = type*4 + nr，其中
	! nr 为 0-3 分别对应软驱 A、B、C 或D；
	! type 是软驱的类型 (2 表示 1.2M 或 7 表示 1.44M 等) 
	! 因为 7*4 + 0 = 28，所以 /dev/PS0 (2,28) 指的是 1.44 M A 驱动器 其设备号是 0x021c
	! 同理 /dev/at0 (2,8)指的是 1.2M A 驱动器，其设备号是 0x0208。

	seg cs
	mov	ax,root_dev

	cmp	ax,#0
	jne	root_defined

	! 取上面保存的每磁道扇区数
	! 如果sectors=15 则说明是 1.2M 的驱动器；
	! 如果sectors=18，则说明是 1.44M 软驱
	! 因为是可引导的驱动器，所以肯定是 A 驱

	! 相当于 mov cs:[bx], [sectors]
	seg cs
	mov	bx,sectors

	mov	ax,#0x0208		! /dev/ps0 - 1.2Mb
	! 判断每磁道扇区数是否 = 15
	cmp	bx,#15

	! 如果等于，则 ax 中就是引导驱动器的设备号
	je	root_defined

	mov	ax,#0x021c		! /dev/PS0 - 1.44Mb

	! 判断每磁道扇区数是否 = 18
	cmp	bx,#18
	je	root_defined
undef_root:
	! 否则，死循环
	jmp undef_root
root_defined:
	! 将检查过的设备号保存起来，相当于 mov cs:[root_dev], ax
	seg cs
	mov	root_dev,ax 

! 到此，所有程序都加载完毕
! 我们就跳转到被加载在 bootsect 后面的 setup 程序去。

	jmpi	0,SETUPSEG

！-----------------------下面是上面需要的函数------------------------------
! 该子程序将内核加载到内存地址 0x10000 处
! 并确定没有跨越 64KB 的内存边界
! 我们试图尽快地进行加载
! 只要可能，就每次加载整条磁道的数据
! 输入：es – 开始内存地址段值 (通常是0x1000) 

! 当前磁道中已读的扇区数。开始时已经读进1 扇区的引导扇区
sread:	.word 1+SETUPLEN

! 当前磁头号。
head:	.word 0

! 当前磁道号
track:	.word 0

read_it:
	! 测试输入的段值。
	! 从盘上读入的数据必须存放在位于内存地址 64KB 的边界开始处，
	! 否则进入死循环。
	mov ax,es
	test ax,#0x0fff
die:
	jne die

	! 清 bx 寄存器，用于表示当前段内存放数据的开始位置。
	xor bx,bx

rp_read:
	! 判断是否已经读入全部数据。
	! 比较当前所读段是否就是系统数据末端所处的段 (#ENDSEG)
	mov ax,es
	cmp ax,#ENDSEG

	! 如果不是就跳转至下面 ok1_read 标号处继续读数据
	jb ok1_read

	! 否则子程序返回。
	ret

ok1_read:

	! 计算和验证当前磁道需要读取的扇区数，放在ax 寄存器中。
	! 根据当前磁道还未读取的扇区数 以及 段内数据字节开始偏移位置
	! 计算如果全部读取这些未读扇区所读总字节数是否会超过 64KB 段长度的限制
	! 若会超过，则根据此次最多能读入的字节数(64KB – 段内偏移位置)
	! 计算出此次需要读取的扇区数

	! 取每磁道扇区数
	seg cs
	mov ax,sectors

	! 减去当前磁道已读扇区数。
	sub ax,sread

	! cx = ax = 当前磁道未读扇区数。
	mov cx,ax

	! cx = cx * 512 字节
	shl cx,#9

	! cx = cx + 段内当前偏移值(bx)，
	! 此次读操作后，段内共读入的字节数
	add cx,bx

	若没有超过 64KB 字节，则跳转至 ok2_read 处执行
	jnc ok2_read
	je ok2_read

	! 若加上此次将读磁道上所有未读扇区时会超过 64KB
	! 则下面三行计算此时最多能读入的字节数(64KB – 段内读偏移位置)
	! 再转换成需要读取的扇区数

	! ax 清零
	xor ax,ax

	! 0 - bx，为剩余字节数，依赖 ax 寄存器只有 64K，减法会溢出
	sub ax,bx

	! 向右移 9 位，相当于 ax /= 512，得到扇区数量
	shr ax,#9

ok2_read:
	! 读取内容
	call read_track

	! cx = 该次操作已读取的扇区数。
	mov cx,ax

	! 当前磁道上已经读取的扇区数。
	add ax,sread

	! 如果当前磁道上的还有扇区未读，则跳转到 ok3_read 处。
	seg cs
	cmp ax,sectors
	jne ok3_read

	! 读该磁道的下一磁头面(1 号磁头)上的数据。如果已经完成，则去读下一磁道。
	mov ax,#1

	! 判断当前磁头号。
	sub ax,head

	! 如果是 0 磁头，则再去读 1 磁头面上的扇区数据。
	jne ok4_read

	! 否则去读下一磁道。
	inc track

ok4_read:
	! 保存当前磁头号。
	mov head,ax

	! 清当前磁道已读扇区数。
	xor ax,ax
ok3_read:
	! 保存当前磁道已读扇区数。
	mov sread,ax

	! 上次已读扇区数*512 字节。
	shl cx,#9

	! 调整当前段内数据开始位置。
	add bx,cx

	! 若小于64KB 边界值，则跳转到 rp_read 处，继续读数据。
	! 否则调整当前段，为读下一段数据作准备。
	jnc rp_read

	! 将段基址调整为指向下一个 64KB 内存开始处。
	mov ax,es
	add ax,#0x1000
	mov es,ax

	! 清段内数据开始偏移值
	xor bx,bx

	! 跳转至 rp_read 处，继续读数据。
	jmp rp_read

read_track:
	! 读当前磁道上指定开始扇区和需读扇区数的数据到 es:bx 开始处
	! al – 需读扇区数；es:bx – 缓冲区开始位置。

	! 保存寄存器
	push ax
	push bx
	push cx
	push dx

	! 读取当前磁道号
	mov dx,track

	! 当前磁道已读扇区数
	mov cx,sread

	! cl = 开始读扇区
	inc cx

	!  ch = 当前磁道号
	mov ch,dl

	! 读取当前磁头号
	mov dx,head

	! dh = 磁头号
	mov dh,dl

	! dl = 驱动器号
	mov dl,#0

	! 确保磁头号 <= 1
	and dx,#0x0100
	
	! 表示读取磁盘
	mov ah,#2

	! 调用读取
	int 0x13

	! 如果出错，跳转到 bad_rt，我猜这个地方应该是 bad_read_truck 的缩写
	jc bad_rt

	! 恢复寄存器
	pop dx
	pop cx
	pop bx
	pop ax

	! 函数返回
	ret

bad_rt:
	! 执行驱动器复位操作
	! 再跳转到 read_track 重试

	! 磁盘复位
	mov ax,#0
	mov dx,#0
	int 0x13

	! 恢复寄存器
	pop dx
	pop cx
	pop bx
	pop ax

	! 跳转重试
	jmp read_track

! 这个子程序用于关闭软驱的马达
! 这样我们进入内核后它处于已知状态
! 以后也就无须担心它了
kill_motor:
	! 保存 dx，下面要改这个寄存器
	push dx

	! 软驱控制器的驱动端口，只写
	mov dx,#0x3f2

	! A 驱动器；
	! 关闭 FDC；
	! 禁止 DMA 和中断请求
	! 关闭马达
	mov al,#0 

	! 将 al 中的内容输出到 dx 指定的端口中
	outb

	! 恢复 dx
	pop dx
	ret

sectors:
	! 存放当前启动软盘每磁道的扇区数
	.word 0

msg1:
	! 需要打印的字符串，其中 13 = '\r', 10 = '\n'
	.byte 13,10
	.ascii "Loading system ..."
	.byte 13,10,13,10

.org 508
root_dev:
	! 根文件系统的设备号
	.word ROOT_DEV
boot_flag:
	! 主引导扇区最后两个字节，必须是 0x55, 0xaa
	! 这样表示主要是由于 Intel 的数据是小端方式表示的
	.word 0xAA55

.text
endtext:
.data
enddata:
.bss
endbss:
