/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

# system_call.s 文件包含系统调用 (system-call) 底层处理子程序
# 由于有些代码比较类似，所以同时也包括时钟中断处理程序，硬盘和软盘的中断处理程序也在这里

# 注意：这段代码处理信号识别，在每次时钟中断和系统调用之后都会进行识别
# 一般中断信号并不处理信号识别，因为会给系统造成混乱

# 从系统调用返回（'ret_from_system_call'）时堆栈的结构：
#	 0(%esp) - %eax
#	 4(%esp) - %ebx
#	 8(%esp) - %ecx
#	 C(%esp) - %edx
#	10(%esp) - %fs
#	14(%esp) - %es
#	18(%esp) - %ds
#	1C(%esp) - %eip
#	20(%esp) - %cs
#	24(%esp) - %eflags
#	28(%esp) - %oldesp
#	2C(%esp) - %oldss

# 定义 SIG_CHLD 信号（子进程停止或结束）
SIG_CHLD	= 17

# 堆栈中各个寄存器的偏移位置
EAX		= 0x00
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24

# 当有特权级变化时
OLDESP		= 0x28

OLDSS		= 0x2C

# 以下这些是任务结构 (task_struct) 中变量的偏移值，参见 include/linux/sched.h

# 进程状态码
state	= 0

# 任务运行时间计数(递减)（滴答数），运行时间片
counter	= 4

// 运行优先数。任务开始运行时 counter=priority，越大则运行时间越长
priority = 8

// 是信号位图，每个比特位代表一种信号，信号值=位偏移值+1
signal	= 12

// sigaction 结构长度必须是 16 字节
// 信号执行属性结构数组的偏移值，对应信号将要执行的操作和标志信息
sigaction = 16

// 受阻塞信号位图的偏移量
blocked = (33*16)

# 以下定义在 sigaction 结构中的偏移量，参见 include/signal.h

# 信号处理过程的句柄（描述符）
sa_handler = 0

# 信号量屏蔽码
sa_mask = 4

# 信号集
sa_flags = 8

# 恢复函数指针，参见 kernel/signal.c
sa_restorer = 12

# Linux 0.11 版内核中的系统调用总数
nr_system_calls = 72

# 好了，在使用软驱时我收到了并行打印机中断，很奇怪。呵，现在不管它

.globl system_call,sys_fork,timer_interrupt,sys_execve
.globl hd_interrupt,floppy_interrupt,parallel_interrupt
.globl device_not_available, coprocessor_error

# 内存 4 字节对齐
.align 2

# 错误的系统调用号
bad_sys_call:
	# eax 中置 -1，退出中断
	movl $-1,%eax
	iret

# 内存 4 字节对齐
.align 2

# 重新执行调度程序入口。调度程序 schedule 在 kernel/sched.c
reschedule:
	pushl $ret_from_sys_call
	jmp schedule

# 内存 4 字节对齐
.align 2

# int 0x80 / linux 系统调用入口点 调用中断int 0x80，eax 中是调用号
system_call:
	# 调用号如果超出范围的话就在 eax 中置 -1 并退出
	cmpl $nr_system_calls-1, %eax
	ja bad_sys_call

	# 保存上下文
	push %ds
	push %es
	push %fs

	# ebx, ecx, edx 中放着系统调用相应的 C 语言函数的调用参数
	pushl %edx
	pushl %ecx
	pushl %ebx

	# 此时已经进入内核态，恢复 ds,es 指向内核数据段 (全局描述符表中数据段描述符)
	movl $0x10,%edx
	mov %dx,%ds
	mov %dx,%es

	# fs 指向局部数据段 (局部描述符表中数据段描述符)
	movl $0x17,%edx
	mov %dx,%fs

	# 下面这句操作数的含义是：调用地址 = sys_call_table + %eax * 4
	# 对应的 C 程序中的 sys_call_table 在 include/linux/sys.h 中
	# 其中定义了一个包括 72 个 系统调用 C 处理函数的地址数组表
	call *sys_call_table(,%eax,4)

	# 把系统调用返回值入栈
	pushl %eax

	# 取当前任务（进程）数据结构地址 -> eax
	movl current,%eax

	# 下面查看当前任务的运行状态。如果不在就绪状态 (state 不等于0) 就去执行调度程序
	cmpl $0,state(%eax)
	jne reschedule

	# 如果该任务在就绪状态，但其时间片已用完（counter=0），则也去执行调度程序
	cmpl $0,counter(%eax)
	je reschedule

# 以下这段代码执行从系统调用 C 函数返回后，对信号量进行识别处理
ret_from_sys_call:

	# 首先判别当前任务是否是初始任务 task 0，如果是则不必对其进行信号量方面的处理，直接返回
	# task 0 不可能有信号
	movl current,%eax
	cmpl task,%eax
	je 3f

	# 通过对原调用程序代码选择符的检查，来判断调用程序是否是内核任务
	# 如果是则直接退出中断，否则对于普通进程则需进行信号量的处理
	# 这里比较选择符是否为普通用户代码段的选择符 0x000f (RPL=3，局部表，第 1 个段(代码段))
	# 如果不是则跳转退出中断程序
	cmpw $0x0f,CS(%esp)
	jne 3f

	# 如果原堆栈段选择符不为 0x17（也即原堆栈不在用户数据段中），则也退出
	cmpw $0x17,OLDSS(%esp)
	jne 3f

	# 下面这段代码的用途是首先取当前任务结构中的信号位图(32 位，每位代表1 种信号)，
	# 然后用任务结构中的信号阻塞（屏蔽）码，阻塞不允许的信号位，取得数值最小的信号值，
	# 再把原信号位图中该信号对应的位复位（置0），最后将该信号值作为参数之一调用 do_signal()
	# do_signal() 在 kernel/signal.c 中，其参数包括 13 个入栈的信息

	# 取信号位图 -> ebx，每 1 位代表 1 种信号，共 32 个信号
	movl signal(%eax),%ebx

	# 取阻塞（屏蔽）信号位图 -> ecx
	movl blocked(%eax),%ecx

	# 每位取反
	notl %ecx

	# 获得许可的信号位图
	andl %ebx,%ecx

	# 从低位（位0）开始扫描位图，看是否有 1 的位
	# 若有，则 ecx 保留该位的偏移值
	bsfl %ecx,%ecx

	# 如果没有信号则向前跳转退出
	je 3f

	# 复位该信号 (ebx 含有原 signal 位图)
	btrl %ecx,%ebx

	# 重新保存 signal 位图信息 -> current->signal
	movl %ebx,signal(%eax)

	# 将信号调整为从 1 开始的数 (1-32)。
	incl %ecx

	# 信号值入栈作为调用 do_signal 的参数之一
	pushl %ecx

	# 调用 C 函数信号处理程序 kernel/signal.c
	call do_signal

	# 调用完成后恢复栈
	popl %eax

3:
	# 恢复上下文
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds

	# 中断返回
	iret

# 内存 4 字节对齐
.align 2

# int 16 / 下面这段代码处理协处理器发出的出错信号
# 跳转执行 C 函数math_error() kernel/math/math_emulate.c
# 返回后将跳转到 ret_from_sys_call 处继续执行
coprocessor_error:

	# 保存上下文
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax

	# 恢复成内核段
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es

	# fs 置为指向局部数据段（出错程序的数据段）
	movl $0x17,%eax
	mov %ax,%fs

	# 把下面调用返回的地址入栈
	pushl $ret_from_sys_call
	# 执行 C 函数 math_error() kernel/math/math_emulate.c
	jmp math_error

	# 上面这两行，假装从 ret_from_sys_call 之前，执行了 call math_error

# 内存 4 字节对齐
.align 2

# int 7 / 设备不存在或协处理器不存在 (Coprocessor not available)
# 如果控制寄存器 CR0 的 EM 标志置位，则当 CPU 执行一个 ESC 转义指令时就会引发该中断
# 这样就可以有机会让这个中断处理程序模拟 ESC 转义指令
# CR0 的 TS 标志是在 CPU 执行任务转换时设置的，TS 可以用来确定什么时候协处理器中的内容（上下文）
# 与 CPU 正在执行的任务不匹配了。当 CPU 在运行一个转义指令时发现 TS 置位了，就会引发该中断
# 此时就应该恢复新任务的协处理器执行状态。参见 kernel/sched.c 中的说明
# 该中断最后将转移到标号 ret_from_sys_call 处执行下去（检测并处理信号）
device_not_available:
	# 保存上下文
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax

	# 恢复成内核段
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es

	# fs 置为指向局部数据段（出错程序的数据段）
	movl $0x17,%eax
	mov %ax,%fs

	# 把下面跳转或调用的返回地址入栈
	pushl $ret_from_sys_call

	# 清除 TS 位
	clts

	movl %cr0,%eax

	# 如果不是 EM 引起的中断，则恢复新任务协处理器状态
	testl $0x4,%eax
	je math_state_restore

	pushl %ebp
	pushl %esi
	pushl %edi

	# 调用 C 函数 math_emulate kernel/math/math_emulate.c
	call math_emulate

	popl %edi
	popl %esi
	popl %ebp

	# 这里的 ret 将跳转到 ret_from_sys_call
	ret

# 内存 4 字节对齐
.align 2

# int 32 / (int 0x20) 时钟中断处理程序。中断频率被设置为 100Hz(include/linux/sched.h)
# 定时芯片 8253/8254 是在 (kernel/sched.c) 处初始化的。因此这里 jiffies 每 10 毫秒加 1
# 这段代码将 jiffies 增 1，发送结束中断指令给 8259 控制器，
# 然后用当前特权级作为参数调用 C 函数do_timer(long CPL)，当调用返回时转去检测并处理信号
timer_interrupt:
	# 保存上下文
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax

	# 恢复成内核段
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es

	# fs 置为指向局部数据段（出错程序的数据段）
	movl $0x17,%eax
	mov %ax,%fs

	# jiffies += 1
	incl jiffies

	# 由于初始化中断控制芯片时没有采用自动 EOI，所以这里需要发指令结束该硬件中断
	movb $0x20,%al
	outb %al,$0x20

	# 下面 3 句从选择符中取出当前特权级别 (0 或 3) 并压入堆栈，作为 do_timer 的参数
	movl CS(%esp),%eax
	andl $3,%eax
	pushl %eax

	# do_timer(CPL) 执行任务切换、计时等工作，在 kernel/shched.c 中实现
	call do_timer

	# 调用完成恢复栈
	addl $4,%esp
	jmp ret_from_sys_call

# 内存 4 字节对齐
.align 2

# 这是 sys_execve() 系统调用，取中断调用程序的代码指针作为参数调用 C 函数 do_execve()
# do_execve() 在 fs/exec.c
sys_execve:
	# 从堆栈中取得中断发生时的 eip -> eax
	lea EIP(%esp),%eax

	# 传递参数
	pushl %eax

	# 调用 do_execve
	call do_execve

	# 恢复栈
	addl $4,%esp
	ret

# 内存 4 字节对齐
.align 2

# sys_fork() 调用，用于创建子进程，是 system_call 功能 2，原形在 include/linux/sys.h 中
sys_fork:
	# 调用 C 函数 find_empty_process()，取得一个进程号 pid。
	call find_empty_process

	# 若返回负数则说明目前任务数组已满
	testl %eax,%eax
	js 1f

	# 否则调用 copy_process() 复制进程
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call copy_process
	addl $20,%esp
1:
	ret

# int 46 / (int 0x2E) 硬盘中断处理程序，响应硬件中断请求 IRQ14
# 当硬盘操作完成或出错就会发出此中断信号，(参见 kernel/blk_drv/hd.c)
# 首先向 8259A 中断控制从芯片发送结束硬件中断指令 (EOI)，然后取变量do_hd 中的函数指针放入edx
# 寄存器中，并置do_hd 为NULL，接着判断edx 函数指针是否为空。如果为空，则给edx 赋值指向
# unexpected_hd_interrupt()，用于显示出错信息。随后向8259A 主芯片送EOI 指令，并调用edx 中
# 指针指向的函数: read_intr()、write_intr()或unexpected_hd_interrupt()。
hd_interrupt:
	# 保存上下文
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs

	# ds,es 置为内核数据段
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es

	# fs 置为调用程序的局部数据段
	movl $0x17,%eax
	mov %ax,%fs

	# 由于初始化中断控制芯片时没有采用自动 EOI，所以这里需要发指令结束该硬件中断
	# 0xA0 表示从 8259A
	movb $0x20,%al
	outb %al,$0xA0

	# 用于延迟
	jmp 1f
1:
	jmp 1f
1:	
	# edx = 0
	xorl %edx,%edx

	# do_hd 定义为一个函数指针
	# 将被赋值 read_intr() 或 write_intr() 函数地址 (kernel/blk_drv/hd.c)
	# 放到edx 寄存器后就将 do_hd 指针变量置为 NULL
	xchgl do_hd,%edx

	# 测试函数指针是否为 NULL
	testl %edx,%edx

	# 若空，则使指针指向 C 函数 unexpected_hd_interrupt()
	jne 1f
	movl $unexpected_hd_interrupt,%edx
1:
	# 送主 8259A 中断控制器 EOI 指令（结束硬件中断）
	outb %al,$0x20

	# 调用 do_hd 指向的 C 函数
	call *%edx

	# 恢复上下文
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

# int 38 / (int 0x26) 软盘驱动器中断处理程序，响应硬件中断请求 IRQ6
# 其处理过程与上面对硬盘的处理基本一样，(kernel/blk_drv/floppy.c)。
# 首先向 8259A 中断控制器主芯片发送EOI 指令，然后取变量do_floppy 中的函数指针放入eax
# 寄存器中，并置do_floppy 为NULL，接着判断eax 函数指针是否为空。如为空，则给eax 赋值指向
# unexpected_floppy_interrupt ()，用于显示出错信息。随后调用eax 指向的函数: rw_interrupt,
# seek_interrupt,recal_interrupt,reset_interrupt 或unexpected_floppy_interrupt。
floppy_interrupt:
	# 保存上下文
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs

	# ds,es 置为内核数据段
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es

	# fs 置为调用程序的局部数据段
	movl $0x17,%eax
	mov %ax,%fs

	# 由于初始化中断控制芯片时没有采用自动 EOI，所以这里需要发指令结束该硬件中断
	movb $0x20,%al
	outb %al,$0x20

	xorl %eax,%eax

	# do_floppy 为一函数指针，将被赋值实际处理 C 函数程序
	# 放到 eax 寄存器后就将 do_floppy 指针变量置空
	xchgl do_floppy,%eax

	# 测试函数指针是否为 NULL
	testl %eax,%eax
	# 若空，则使指针指向 C 函数 unexpected_floppy_interrupt()
	jne 1f

	movl $unexpected_floppy_interrupt,%eax
1:
	# 调用 do_floppy 指向的函数
	call *%eax

	# 恢复上下文
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret

# int 39 / (int 0x27) 并行口中断处理程序，对应硬件中断请求信号 IRQ7
# 本版本内核还未实现。这里只是发送EOI 指令
parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
