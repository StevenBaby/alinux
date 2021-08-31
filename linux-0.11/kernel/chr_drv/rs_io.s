/*
 *  linux/kernel/rs_io.s
 *
 *  (C) 1991  Linus Torvalds
 */

# 该程序模块实现 rs232 输入输出中断处理程序

.text
.globl rs1_interrupt,rs2_interrupt

# size 是读写队列缓冲区的字节长度
# 必须是 2 的次方并且需与 tty_io.c 中的值匹配!
size	= 1024

# 以下这些是读写缓冲结构中的偏移量
# 对应定义在 include/linux/tty.h 文件中 tty_queue 结构中各变量的偏移量

# 串行端口号字段偏移
rs_addr = 0

# 缓冲区中头指针字段偏移
head = 4

# 缓冲区中尾指针字段偏移
tail = 8

# 等待该缓冲的进程字段偏移
proc_list = 12

# 缓冲区字段偏移
buf = 16

# 当写队列里还剩 256 个字符空间 (WAKEUP_CHARS) 时，我们就可以写
startup	= 256	/* chars left in write queue when we restart it */

# 这些是实际的中断程序
# 程序首先检查中断的来源，然后执行相应的处理
.align 2
# 串行端口1 中断处理程序入口点
rs1_interrupt:
	# tty 表中对应串口1 的读写缓冲指针的地址入栈
	pushl $table_list+8

	# 字符缓冲队列结构格式请参见 include/linux/tty.h
	jmp rs_int

.align 2
rs2_interrupt:
	# 串行端口2 中断处理程序入口点
	pushl $table_list+16
rs_int:
	# 保存上下文
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	push %es
	push %ds
	
	# 由于这是一个中断程序，我们不知道 ds 是否正确
	# 所以加载它们(让ds、es 指向内核数据段
	pushl $0x10
	pop %ds
	pushl $0x10
	pop %es

	# 将缓冲队列指针地址存入 edx 寄存器
	# 也即上面最先压入堆栈的地址
	movl 24(%esp),%edx

	# 取读缓冲队列结构指针(地址) -> edx
	# 对于串行终端，data 字段存放着串行端口地址（端口号）
	movl (%edx),%edx

	# 取串口1（或串口2）的端口号 -> edx
	movl rs_addr(%edx),%edx

	# edx 指向中断标识寄存器
	addl $2,%edx
	# 中断标识寄存器端口是 0x3fa（0x2fa）
rep_int:

	# eax 清零
	xorl %eax,%eax

	# 取中断标识字节，用以判断中断来源(有4 种中断情况)
	inb %dx,%al

	# 首先判断有无待处理的中断(位0=1 无中断；=0 有中断)
	testb $1,%al

	# 若无待处理中断，则跳转至退出处理处 end
	jne end

	# 这不会发生，但是 ……
	cmpb $6,%al

	# al 值>6? 是则跳转至end（没有这种状态）
	ja end

	# 再取缓冲队列指针地址 -> ecx
	movl 24(%esp),%ecx

	# 将中断标识寄存器端口号 0x3fa(0x2fa) 入栈
	pushl %edx

	# 0x3f8(0x2f8)
	subl $2,%edx

	# 不乘4，位 0 已是 0
	call *jmp_table(,%eax,2)		/* NOTE! not *4, bit0 is 0 already */

	# 上面语句是指，当有待处理中断时，al 中位 0=0，位 2-1 是中断类型
	# 因此相当于已经将中断类型乘了 2，这里再乘 2，
	# 获得跳转表对应各中断类型地址，并跳转到那里去作相应处理
	# 中断来源有 4 种：
		# modem 状态发生变化；
		# 要写（发送）字符；
		# 要读（接收）字符；
		# 线路状态发生变化
	# 要发送字符中断是通过设置发送保持寄存器标志实现的
	# 在 serial.c 程序中的 rs_write() 函数中
	# 当写缓冲队列中有数据时，就会修改中断允许寄存器内容
	# 添加上发送保持寄存器中断允许标志，从而在系统需要发送字符时引起串行中断发生

	# 弹出中断标识寄存器端口号 0x3fa（或0x2fa）
	popl %edx

	# 跳转，继续判断有无待处理中断并继续处理
	jmp rep_int
end:
	# 向中断控制器发送结束中断指令 EOI
	movb $0x20,%al
	outb %al,$0x20		/* EOI */

	# 恢复上下文
	pop %ds
	pop %es
	popl %eax
	popl %ebx
	popl %ecx
	popl %edx

	# 丢弃缓冲队列指针地址
	addl $4,%esp		# jump over _table_list entry
	iret

# 各中断类型处理程序地址跳转表，共有 4 种中断来源：
# modem 状态变化中断
# 写字符中断
# 读字符中断
# 线路状态有问题中断
jmp_table:
	.long modem_status,write_char,read_char,line_status

# 由于 modem 状态发生变化而引发此次中断
# 通过读 modem 状态寄存器对其进行复位操作
.align 2
modem_status:
	# 通过读 modem 状态寄存器进行复位(0x3fe)
	addl $6,%edx
	inb %dx,%al
	ret

# 由于线路状态发生变化而引起这次串行中断
# 通过读线路状态寄存器对其进行复位操作
.align 2
line_status:
	# 通过读线路状态寄存器进行复位(0x3fd)
	addl $5,%edx
	inb %dx,%al
	ret

# 由于串行设备（芯片）接收到字符而引起这次中断
# 将接收到的字符放到读缓冲队列read_q 头指针（head）处
# 并且让该指针前移一个字符位置
# 若 head 指针已经到达缓冲区末端，则让其折返到缓冲区开始处
# 最后调用 C 函数 do_tty_interrupt()，也即 copy_to_cooked()
# 把读入的字符经过一定处理，放入规范模式缓冲队列（辅助缓冲队列 secondary）中
.align 2
read_char:
	# 读取字符 -> al
	inb %dx,%al

	# 当前串口缓冲队列指针地址 -> edx
	movl %ecx,%edx

	# 缓冲队列指针表首址 - 当前串口队列指针地址 -> edx
	subl $table_list,%edx

	# 差值/8，对于串口1 是 1，对于串口2 是 2
	shrl $3,%edx

	# 取读缓冲队列结构地址 -> ecx
	movl (%ecx),%ecx		# read-queue

	# 取读队列中缓冲头指针 -> ebx
	movl head(%ecx),%ebx

	# 将字符放在缓冲区中头指针所指的位置
	movb %al,buf(%ecx,%ebx)

	# 将头指针前移一字节
	incl %ebx

	# 用缓冲区大小对头指针进行模操作，指针不能超过缓冲区大小
	andl $size-1,%ebx

	# 缓冲区头指针与尾指针比较
	cmpl tail(%ecx),%ebx

	# 若相等，表示缓冲区满，跳转到标号1 处
	je 1f

	# 保存修改过的头指针
	movl %ebx,head(%ecx)
1:
	# 将串口号压入堆栈(1- 串口1，2 - 串口2)，作为参数
	pushl %edx

	# 调用 tty 中断处理 C 函数
	call do_tty_interrupt

	# 丢弃入栈参数，并返回
	addl $4,%esp
	ret

# 由于设置了发送保持寄存器允许中断标志而引起此次中断
# 说明对应串行终端的写字符缓冲队列中有字符需要发送
# 于是计算出写队列中当前所含字符数
# 若字符数已小于 256 个则唤醒等待写操作进程
# 然后从写缓冲队列尾部取出一个字符发送，并调整和保存尾指针
# 如果写缓冲队列已空，则跳转到 write_buffer_empty，处理写缓冲队列空的情况
.align 2
write_char:
	# 取写缓冲队列结构地址 -> ecx
	movl 4(%ecx),%ecx		# write-queue

	# 取写队列头指针 -> ebx
	movl head(%ecx),%ebx

	# 头指针 - 尾指针 = 队列中字符数
	subl tail(%ecx),%ebx

	# 对指针取模运算
	andl $size-1,%ebx		# nr chars in queue

	# 如果头指针 = 尾指针，说明写队列无字符，跳转处理
	je write_buffer_empty

	# 队列中字符数超过 256 个？
	cmpl $startup,%ebx

	# 超过，则跳转处理
	ja 1f

	# 唤醒等待的进程
	# 取等待该队列的进程的指针，并判断是否为空
	movl proc_list(%ecx),%ebx	# wake up sleeping process

	# 有等待的进程吗？
	testl %ebx,%ebx			# is there any?

	# 是空的，则向前跳转到标号1 处
	je 1f

	# 否则将进程置为可运行状态(唤醒进程)。。
	movl $0,(%ebx)
1:
	# 取尾指针
	movl tail(%ecx),%ebx

	# 从缓冲中尾指针处取一字符 -> al
	movb buf(%ecx,%ebx),%al

	# 向端口 0x3f8(0x2f8) 送出到保持寄存器中
	outb %al,%dx

	# 尾指针前移
	incl %ebx

	# 尾指针若到缓冲区末端，则折回
	andl $size-1,%ebx

	# 保存已修改过的尾指针
	movl %ebx,tail(%ecx)

	# 尾指针与头指针比较
	cmpl head(%ecx),%ebx

	# 若相等，表示队列已空，则跳转
	je write_buffer_empty
	ret

# 处理写缓冲队列 write_q 已空的情况，若有等待写该串行终端的进程则唤醒之
# 然后屏蔽发送保持寄存器空中断，不让发送保持寄存器空时产生中断
.align 2
write_buffer_empty:

	# 唤醒等待的进程
	# 取等待该队列的进程的指针，并判断是否为空
	movl proc_list(%ecx),%ebx	# wake up sleeping process

	# 有等待的进程吗？
	testl %ebx,%ebx			# is there any?

	# 无，则向前跳转到标号1 处
	je 1f

	# 否则将进程置为可运行状态(唤醒进程)
	movl $0,(%ebx)
1:
	# 指向端口 0x3f9(0x2f9)
	incl %edx

	# 读取中断允许寄存器
	inb %dx,%al

	# 稍作延迟。
	jmp 1f
1:
	jmp 1f
1:
	# 屏蔽发送保持寄存器空中断（位1）
	andb $0xd,%al

	# 写入 0x3f9(0x2f9)
	outb %al,%dx
	ret
