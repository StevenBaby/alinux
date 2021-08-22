/*
 *  linux/mm/page.s
 *
 *  (C) 1991  Linus Torvalds
 */

# page.s 程序包含底层页异常处理代码。实际的工作在 memory.c 中完成

.globl page_fault

page_fault:
	# 取出错码到 eax，保存上下文
	xchgl %eax,(%esp)
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs

	# 置内核数据段选择符
	movl $0x10,%edx
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs

	# 取引起页面异常的线性地址
	movl %cr2,%edx

	# 将该线性地址和出错码压入堆栈，作为调用函数的参数
	pushl %edx
	pushl %eax

	# 测试标志P，如果不是缺页引起的异常则跳转
	testl $1,%eax
	jne 1f
	
	# 调用缺页处理函数
	call do_no_page
	jmp 2f
1:
	# 调用写保护处理函数
	call do_wp_page
2:
	# 丢弃压入栈的两个参数
	addl $8,%esp

	# 恢复上下文
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret
