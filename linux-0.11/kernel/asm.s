/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

# asm.s 程序中包括大部分的硬件故障（或出错）处理的底层次代码
# 页异常 PageFault 是由内存管理程序 mm 处理的，所以不在这里
# 此程序还处理（希望是这样）由于 TS 位而造成的 fpu 异常
# 因为fpu 必须正确地进行保存/恢复处理，这些还没有测试过

# 这个文件主要处理 Intel 定义的 (中断/异常)
# 软中断、外中断，异常 的实现方式都是一样的，都是一个中断函数，最后用 iret 返回
# 区别在于
# 异常是 Intel 在 CPU 中定义的
# 外中断 由外部硬件触发，比如时钟，键盘
# 内终端 由 int 指令触发

# 本代码文件主要涉及对 Intel 保留的中断 int0 ~ int16 的处理 (int 17 ~ int 31 留作今后使用)
# 以下是一些全局函数名的声明，其原形在 traps.c 中说明

.globl divide_error,debug,nmi,int3,overflow,bounds,invalid_op
.globl double_fault,coprocessor_segment_overrun
.globl invalid_TSS,segment_not_present,stack_segment
.globl general_protection,coprocessor_error,irq13,reserved

# int 0
# 下面是被零除出错 (divide_error) 处理代码。'do_divide_error'函数在traps.c 中
# 那个年代的 gcc 编译生成的符号名，前面会加上下划线 _，但是现在已经不用了，故所有的前缀下划线都去掉了。
divide_error:
	# 首先把将要调用的函数地址入栈，这段程序的出错号为 0
	pushl $do_divide_error

# 这里是无出错号处理的入口处
no_error_code:
	# do_divide_error 的地址 -> eax，eax 被交换入栈。
	xchgl %eax,(%esp)

	# 保存上下文
	pushl %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp

	# 注：16 位的段寄存器入栈后也要占用 4 个字节
	push %ds
	push %es
	push %fs

	# 将出错码入栈
	pushl $0

	# 取原调用返回地址处堆栈指针位置，并压入堆栈
	lea 44(%esp),%edx
	pushl %edx

	# 此时已进入内核态，恢复内核代码数据段选择符
	movl $0x10,%edx
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs

	# 调用 实际的处理函数，一开始压入栈中的那个
	call *%eax

	# 恢复堆栈，让堆栈指针重新指向寄存器 fs 入栈处
	addl $8,%esp

	# 恢复上下文，与上面压栈相对，后进先出
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax

	# 中断返回
	iret

# int 1 / debug 调试中断入口点
debug:
	# do_debug C 函数指针入栈，以下同
	pushl $do_int3
	jmp no_error_code

# int 2 / 非屏蔽中断调用入口点
nmi:
	pushl $do_nmi
	jmp no_error_code

# int 3
int3:
	pushl $do_int3
	jmp no_error_code

# int 4 / 溢出出错处理中断入口点
overflow:
	pushl $do_overflow
	jmp no_error_code

# int 5 / 边界检查出错中断入口点
bounds:
	pushl $do_bounds
	jmp no_error_code

# int 6 / 无效操作指令出错中断入口点
invalid_op:
	pushl $do_invalid_op
	jmp no_error_code

# int 9 / 协处理器段超出出错中断入口点
coprocessor_segment_overrun:
	pushl $do_coprocessor_segment_overrun
	jmp no_error_code

# int 15 – 保留
reserved:
	pushl $do_reserved
	jmp no_error_code

# int45 / ( = 0x20 + 13 ) 数学协处理器 (Coprocessor) 发出的中断
# 当协处理器执行完一个操作时就会发出 IRQ13 中断信号，以通知 CPU 操作完成
irq13:
	pushl %eax

	# 80387 在执行计算时，CPU 会等待其操作的完成
	xorb %al,%al

	# 通过写 0xF0 端口，本中断将消除 CPU 的 BUSY 延续信号
	# 并重新激活 80387 的处理器扩展请求引脚 PEREQ
	# 该操作主要是为了确保在继续执行 80387 的任何指令之前，响应本中断
	outb %al,$0xF0

	# 向 8259 主中断控制芯片发送 EOI（中断结束）信号。

	movb $0x20,%al
	outb %al,$0x20

	# 这两个跳转指令起延时作用
	jmp 1f
1:
	jmp 1f
1:
	# 再向 8259 从中断控制芯片发送EOI (中断结束) 信号
	outb %al,$0xA0
	popl %eax

	# coprocessor_error 原来在本文件中，现在已经放到 kernel/system_call.s
	jmp coprocessor_error

# 以下中断在调用时会在中断返回地址之后，将出错号压入堆栈，因此返回时也需要将出错号弹出
# int 8 / 双出错故障
double_fault:
	# C 函数地址入栈
	pushl $do_double_fault

error_code:
	# error code <-> %eax，eax 原来的值被保存在堆栈上
	xchgl %eax,4(%esp)

	# &function <-> %ebx，ebx 原来的值被保存在堆栈上
	xchgl %ebx,(%esp)

	# 保存上下文
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs

	# 将出错码入栈
	pushl %eax

	# 取原调用返回地址处堆栈指针位置，并压入堆栈
	lea 44(%esp),%eax		# offset
	pushl %eax

	# 此时已进入内核态，恢复内核代码数据段选择符
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs

	# 调用 实际的处理函数，一开始压入栈中的那个
	call *%ebx

	# 恢复堆栈，让堆栈指针重新指向寄存器 fs 入栈处
	addl $8,%esp

	# 恢复上下文
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax

	# 中断返回
	iret

# int 10 / 无效的任务状态段(TSS)
invalid_TSS:
	pushl $do_invalid_TSS
	jmp error_code

# int 11 / 段不存在
segment_not_present:
	pushl $do_segment_not_present
	jmp error_code

# int 12 / 堆栈段错误
stack_segment:
	pushl $do_stack_segment
	jmp error_code

# int 13 / 一般保护性出错
general_protection:
	pushl $do_general_protection
	jmp error_code

# int 7 / 设备不存在 device_not_available 在 kernel/system_call.s
# int 14 / 页错误 page_fault 在 mm/page.s
# int 16 / 协处理器错误 coprocessor_error 在 kernel/system_call.s
# 时钟中断 int 0x20 timer_interrupt 在 kernel/system_call.s
# 系统调用 int 0x80 system_call 在 kernel/system_call.s
