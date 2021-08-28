
// 移动到用户模式运行
// 该函数利用 iret 指令实现从内核模式移动到初始任务 0 中去执行

// 保存堆栈指针 esp 到 eax 寄存器中
// 首先将堆栈段选择子 (ss) 入栈
// 然后将保存的堆栈指针值 esp 入栈
// 将标志寄存器(eflags)内容入栈
// 将 Task0 代码段选择子(cs) 入栈
// 将下面标号1 的偏移地址(eip) 入栈
// 执行中断返回指令，则会跳转到下面标号1 处
// 此时开始执行任务0
// 初始化段寄存器指向本局部表的数据段
#define move_to_user_mode()            \
	__asm__("movl %%esp,%%eax\n\t"     \ 
			"pushl $0x17\n\t"          \
			"pushl %%eax\n\t"          \
			"pushfl\n\t"               \
			"pushl $0x0f\n\t"          \
			"pushl $1f\n\t"            \
			"iret\n"                   \
			"1:\tmovl $0x17,%%eax\n\t" \
			"movw %%ax,%%ds\n\t"       \
			"movw %%ax,%%es\n\t"       \
			"movw %%ax,%%fs\n\t"       \
			"movw %%ax,%%gs" ::        \
				: "ax")

// 开中断 set interrupt
#define sti() __asm__("sti" ::)

// 关中断 clear interrupt
#define cli() __asm__("cli" ::)

// 空操作 no operation
#define nop() __asm__("nop" ::)

// 中断返回
#define iret() __asm__("iret" ::)

// 设置门描述符宏函数
// 参数：
// gate_addr -描述符地址；
// type -描述符中类型域值；
// dpl -描述符特权层值；
// addr - 偏移地址
// %0 - (由 dpl, type 组合成的类型标志字)；%1 - (描述符低 4 字节地址)；
#define _set_gate(gate_addr, type, dpl, addr)                   \
	__asm__("movw %%dx,%%ax\n\t"                                \
			"movw %0,%%dx\n\t"                                  \
			"movl %%eax,%1\n\t"                                 \
			"movl %%edx,%2"                                     \
			:                                                   \
			: "i"((short)(0x8000 + (dpl << 13) + (type << 8))), \
			  "o"(*((char *)(gate_addr))),                      \
			  "o"(*(4 + (char *)(gate_addr))),                  \
			  "d"((char *)(addr)), "a"(0x00080000))

// 设置中断门函数
// 参数：n - 中断号；addr - 中断程序偏移地址
// &idt[n]对应中断号在中断描述符表中的偏移值；中断描述符的类型是 14，特权级是 0
#define set_intr_gate(n, addr) \
	_set_gate(&idt[n], 14, 0, addr)

// 设置陷阱门函数
// 参数：n - 中断号；addr - 中断程序偏移地址
// &idt[n] 对应中断号在中断描述符表中的偏移值；中断描述符的类型是 15，特权级是 0
#define set_trap_gate(n, addr) \
	_set_gate(&idt[n], 15, 0, addr)

// 设置系统调用门函数
// 参数：n - 中断号；addr - 中断程序偏移地址
// &idt[n] 对应中断号在中断描述符表中的偏移值；中断描述符的类型是 15，特权级是 3
#define set_system_gate(n, addr) \
	_set_gate(&idt[n], 15, 3, addr)

// 设置段描述符函数
// 参数：gate_addr - 描述符地址；type - 描述符中类型域值；dpl - 描述符特权层值；
// base - 段的基地址；limit - 段限长（参见段描述符的格式）
#define _set_seg_desc(gate_addr, type, dpl, base, limit)   \
	{                                                      \
		*(gate_addr) = ((base)&0xff000000) |               \
					   (((base)&0x00ff0000) >> 16) |       \
					   ((limit)&0xf0000) |                 \
					   ((dpl) << 13) |                     \
					   (0x00408000) |                      \
					   ((type) << 8);                      \
		*((gate_addr) + 1) = (((base)&0x0000ffff) << 16) | \
							 ((limit)&0x0ffff);            \
	}

// 在全局表中设置任务状态段/局部表描述符
// 参数：
// n - 在全局表中描述符项 n 所对应的地址；
// addr - 状态段/局部表所在内存的基地址
// type - 描述符中的标志类型字节
// %0 - eax(地址 addr)；
// %1 - (描述符项 n 的地址)；
// %2 - (描述符项 n 的地址偏移 2 处)；
// %3 - (描述符项 n 的地址偏移 4 处)；
// %4 - (描述符项 n 的地址偏移 5 处)；
// %5 - (描述符项 n 的地址偏移 6 处)；
// %6 - (描述符项 n 的地址偏移 7 处)；
#define _set_tssldt_desc(n, addr, type)              \
	__asm__("movw $104,%1\n\t"                       \
			"movw %%ax,%2\n\t"                       \
			"rorl $16,%%eax\n\t"                     \
			"movb %%al,%3\n\t"                       \
			"movb $" type ",%4\n\t"                  \
			"movb $0x00,%5\n\t"                      \
			"movb %%ah,%6\n\t"                       \
			"rorl $16,%%eax" ::"a"(addr),            \
			"m"(*(n)), "m"(*(n + 2)), "m"(*(n + 4)), \
			"m"(*(n + 5)), "m"(*(n + 6)), "m"(*(n + 7)))

// 在全局表中设置任务状态段描述符
// n - 是该描述符的指针；
// addr - 是描述符中的基地址值
// 任务状态段描述符的类型是 0x89
#define set_tss_desc(n, addr) _set_tssldt_desc(((char *)(n)), ((int)(addr)), "0x89")

// 在全局表中设置局部表描述符
// n - 是该描述符的指针；
// addr - 是描述符中的基地址值
// 局部表描述符的类型是 0x82
#define set_ldt_desc(n, addr) _set_tssldt_desc(((char *)(n)), ((int)(addr)), "0x82")
