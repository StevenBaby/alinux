/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */


# head.s 含有 32 位启动代码。
# 注意!!! 32 位启动代码是从绝对地址 0x00000000 开始的
# 这里也同样是页目录将存在的地方，
# 因此这里的启动代码将被页目录覆盖掉

.text
.globl idt,gdt,pg_dir,tmp_floppy_area
pg_dir: # 页目录将会存放在这里
.globl startup_32
startup_32:
	# system 模块 的入口地址在这里 setup.s 跳转而来

	xchg %bx, %bx

	# 下面设置各个数据段寄存器为数据段选择子
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs

	# 表示 stack_start -> ss:esp
	# 设置系统堆栈
	lss stack_start,%esp

	# 设置中断描述符表
	call setup_idt

	# 设置全局描述符表
	call setup_gdt

	# 重新加载所有的数据段寄存器
	# CS 段的寄存器已经在 setup_gdt 中设置过了
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs

	# 设置系统堆栈
	lss stack_start,%esp

	# 下面的代码用于测试 A20 线是否已经开启
	# 采用的方法是向内存地址0x000000 处写入任意数值
	# 然后看内存地址 0x100000(1M) 处是否也是这个数值
	# 如果一直相同的话，就一直比较下去，也即死循环、死机
	# 表示地址 A20 线没有选通，结果内核就不能使用 1M 以上内存
	xorl %eax,%eax
1:
	incl %eax
	movl %eax,0x000000
	cmpl %eax,0x100000

	# 1b 表示向后跳转到 1 标号
	je 1b 

	# 注意! 在下面这段程序中，486 应该将位 16 置位，以检查在超级用户模式下的写保护
	# 此后 verify_area() 调用中就不需要了。486 的用户通常也会想将 NE(#5) 置位，
	# 以便对数学协处理器的出错使用 int 16。

	# 检查数学协处理器芯片
	movl %cr0,%eax

	# 保存 SG, PE, ET 位
	andl $0x80000011,%eax

	# 设置 MP 位
	orl $2,%eax

	# 设置 cr0 寄存器
	movl %eax,%cr0

	# 检测 x87 芯片
	call check_x87

	# 跳转到 after_page_tables
	jmp after_page_tables

# 我们依赖于 ET 标志的正确性来检测 287/387 存在与否

check_x87:

	# 初始化 FPU，无需检测浮点异常
	fninit

	# 将浮点状态寄存器存入 ax
	fstsw %ax

	# 如果存在的则向前跳转到标号 1 处，否则改写cr0
	cmpb $0,%al

	# 1f 表示向前跳转到 1 标记
	je 1f

	movl %cr0,%eax

	# 设置 EM 位，清除 MP 位
	xorl $6,%eax

	# 重新设置 cr0
	movl %eax,%cr0
	ret

.align 2
1:
	# 287 协处理器码，浮点设置保护
	.byte 0xDB,0xE4
	ret

# 下面这段是设置中断描述符表子程序 setup_idt
# 
# 将中断描述符表 idt 设置成具有 256 个项，并都指向 ignore_int 中断门
# 然后加载中断描述符表寄存器(用 lidt 指令)，真正使用的中断门以后再安装
# 当我们在其它地方认为一切都正常时再开启中断，该子程序将会被页表覆盖掉

# 中断描述符表中的项虽然也是 8 字节组成
# 但其格式与全局表中的不同，是一种门描述符 (Gate Descriptor)
# 它的 0-1, 6-7 字节是偏移量，2-3 字节是选择符，4-5 字节是一些标志

setup_idt:
	# 将 ignore_int 的有效地址写入 edx 寄存器
	lea ignore_int, %edx

	# 将选择符 0x0008 置入 eax 的高 16 位中
	movl $0x00080000, %eax

	# 偏移值的低 16 位置入 eax 的低 16 位中
	# 此时 eax 含有门描述符低 4 字节的值
	movw %dx,%ax

	# 此时 edx 含有门描述符高 4 字节的值
	movw $0x8E00, %dx

	# idt 是中断描述符表的地址
	lea idt,%edi

	# 总共有 256 个中断描述符
	mov $256,%ecx

# 我猜下面这个标记应该是 repeat_set_idt
rp_sidt:
	# 循环设置所有 256 个描述符
	movl %eax,(%edi)
	movl %edx,4(%edi)
	addl $8,%edi
	dec %ecx
	# 不为 0 时跳转
	jne rp_sidt 

	# 加载中断描述符 idt_descr 位中断描述符表的指针
	lidt idt_descr
	ret

# 设置全局描述符表项 setup_gdt
# 这个子程序设置一个新的全局描述符表gdt，并加载。
# 此时仅创建了两个表项，与前面的一样。该子程序只有两行，
# “非常的”复杂，所以当然需要这么长的注释了 😊。
# 该子程序将被页表覆盖掉

setup_gdt:
	# 使用全局描述符指针，加载全局描述符表寄存器
	lgdt gdt_descr
	ret

# Linus 将内核的内存页表直接放在页目录之后
# 使用了 4 个表来寻址 16Mb 的物理内存
# 如果你有多于 16Mb 的内存，就需要在这里进行扩充修改

.org 0x1000
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000

# 当 DMA（直接存储器访问）不能访问缓冲块时
# 下面的 tmp_floppy_area 内存块就可供软盘驱动程序使用
# 其地址需要对齐调整，这样就不会跨越 64kB 边界

tmp_floppy_area:
	# 临时的软盘缓冲区
	# .fill repeat , size , value
	# 格式为 重复 1024 次，每次 1 个字节，填充的内容位 0
	.fill 1024,1,0

# 下面这几个入栈操作(pushl)用于为调用 /init/main.c 程序和返回作准备
# 前面 3 个入栈 0 值应该分别是 envp、argv 指针和 argc 值，但 main() 没有用到
# pushl $L6 入栈操作是模拟调用 main.c 程序时首先将返回地址入栈的操作
# 所以如果 main.c 程序真的退出时，就会返回到这里的标号 L6 处继续执行下去，也即死循环
# pushl $_main 将 main.c 的地址压入堆栈，这样，在设置分页处理（setup_paging）结束后
# 执行 'ret' 返回指令时就会将 main.c 程序的地址弹出堆栈，并去执行 main.c 程序

after_page_tables:
	# 这些是调用 main 程序的参数
	pushl $0
	pushl $0
	pushl $0

	# L6 位 main 函数的返回地址，如果真的会返回的话
	pushl $L6

	# 模拟 ret 指令对应 call 引发的 eip 入栈地址
	# 这样后面调用 ret 指令，实际上相当于 add esp, 4; jmp main
	pushl $main

	# 跳转到 setup_paging 设置内存映射
	jmp setup_paging
L6:
	# main 函数应该永远不会到达此处
	# 如果真的到这儿了，你应该知道你干了啥
	jmp L6

# 下面是默认的中断处理程序
int_msg:
	.asciz "Unknown interrupt\n\r"
.align 2
ignore_int:

	# 保存任务的一些必要寄存器
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs

	# 设置内核数据段
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs

	# 为 printk 传递参数 就是上面那句话
	pushl $int_msg
	# 调用 printk
	call printk

	# 调用完成之后恢复栈
	# 可以直接使用 addl $4, %esp，效果更佳，这里只用弹栈，eax 中的值实际上并没有用
	popl %eax

	# 恢复上面设置的寄存器
	# 注意后进先出，应该和上面的顺序是反着的
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax

	# 中断返回
	iret

# 以下为内存映射

# 这个子程序通过设置控制寄存器 cr0 的标志 (PG 位 31）来启动对内存的分页处理功能
# 并设置各个页表项的内容，以恒等映射前 16 MB 的物理内存
# 分页器假定不会产生非法的地址映射（也即在只有 4Mb 的机器上设置出大于 4Mb 的内存地址）

# 注意！尽管所有的物理地址都应该由这个子程序进行恒等映射
# 但只有内核页面管理函数能直接使用 >1Mb 的地址。所有“一般”函数仅使用低于 1Mb 的地址空间
# 或者是使用局部数据空间，地址空间将被映射到其它一些地方去，mm(内存管理程序)会管理这些事的

# 那些有多于 16Mb 内存的家伙，真是太卧槽了，我都没有，为什么你会有 😊。
# 代码就在这里，你自己看着改吧。
# 实际上，这并不太困难的。通常只需修改一些常数等。我把它设置为 16Mb
# 因为我的机器再怎么扩充甚至不能超过这个界限（当然，我的机器是很便宜的 😂）
# 我已经通过设置某类标志来给出需要改动的地方（搜索“16Mb”）
# 但我不能保证作这些改动就行了，也就是后果自负 😜

# 在内存物理地址 0x0 处开始存放 1 个页目录表 和 4 页页表
# 页目录表是系统所有进程公用的，而这里的 4 页页表则是属于内核专用
# 对于新的进程，系统会在主内存区为其申请页面存放页表
# 1 页内存长度是 4096 字节 = 4KB

# 按 4 字节方式对齐内存地址边界。
.align 2
setup_paging:
	# 首先对 5 页内存（1 页目录 + 4 页页表）清零
	# 一次操作 4 个字节，一页总共有 1024 个入口
	movl $1024 * 5,%ecx

	# 清空 eax 寄存器
	xorl %eax,%eax

	# 清空 edi 寄存器，页目录是从 0x000 开始的
	xorl %edi,%edi

	# 清空方向标志，从小到大设置
	cld

	# 重复执行 ecx 次 stosl 指令
	# 每次设置 eax 到 es:edi 指向的内存，然后 edi += 4
	rep;
	stosl

	# 清零完毕，下面开始设置

	# 7 = 0b111，表示 页在内存中 / 用户页 / 可读可写


	# 依次设置映射每个 4M 内存所使用的页表，总共 4 个 4M
	movl $pg0+7,pg_dir
	movl $pg1+7,pg_dir+4
	movl $pg2+7,pg_dir+8
	movl $pg3+7,pg_dir+12

	# 下面填写 4 个页表中的所有内容

	# 设置最后一个页表入口 -> edi
	movl $pg3+4092,%edi

	# 0xfff000 是 16M 内存的最后一页，入口地址
	# 7 是页表属性
	movl $0xfff007,%eax

	# 设置方向标志，从后往前赋值
	std
1:
	# eax -> es:edi，然后将 edi -= 4(由于前面 std)
	stosl
	# 设置下一个页表地址
	subl $0x1000,%eax
	jge 1b

	# 页目录的位置在 0x000
	xorl %eax,%eax

	# cr3 寄存器中存储的是 页目录
	movl %eax,%cr3

	# 准备修改 cr0 寄存器
	movl %cr0,%eax

	# 设置 PG 位，第 31 位
	orl $0x80000000,%eax

	# 重新设置 cr0 寄存器
	movl %eax,%cr0

	# 在改变分页处理标志后要求使用转移指令刷新预取指令队列，这里用的是返回指令 ret
	# 该返回指令的另一个作用是将堆栈中的 main 程序的地址弹出，并开始运行 /init/main.c 程序。
	# 本程序到此真正结束了。
	ret

.align 2
.word 0
idt_descr:
	# 中断描述符指针，指明了中断描述符表在内存中的位置，及长度（实际为界限，也就是长度 - 1）
	.word 256*8-1
	.long idt

.align 2
.word 0
gdt_descr:
	# 全局描述符指针，指明了全局描述符表在内存中的位置，及长度（实际为界限，也就是长度 - 1）
	.word 256*8-1
	.long gdt

.align 8
idt:
	# 默认中断描述符表未初始化
	# 256 * 8 个字节，全部都是 0
	.fill 256,8,0

gdt:
	# 空描述符，这是 Intel 要求必须的
	.quad 0x0000000000000000

	# 16M 32 位代码段
	.quad 0x00c09a0000000fff

	# 16M 32 位数据段
	.quad 0x00c0920000000fff

	# 临时的，别用
	.quad 0x0000000000000000

	# 剩下的为其他 252 个描述符预留空间
	.fill 252,8,0
