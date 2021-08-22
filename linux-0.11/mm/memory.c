/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 按需加载是从 91.01.12 开始编写的
// 在程序编制表中似乎是最重要的程序，并且应该是很容易实现的 - linus

// 好吧，按需加载是比较容易编写的，而共享页面却需要有点技巧
// 共享页面程序是 91.02.12 开始编写的，好象能够工作 - Linus。

// 通过执行大约 30 个 /bin/sh 对共享操作进行了测试：
// 在老内核当中需要占用多于 6M 的内存，而目前却不用，现在看来工作得挺好

// 对 invalidate() 函数也进行了修正 - 在这方面我还做的不够

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

// 进程退出处理函数，在 kernel/exit.c
void do_exit(long code);

// 显示内存已用完出错信息，并退出
static inline void oom(void)
{
	printk("out of memory\n\r");
	// do_exit() 应该使用退出代码，这里用了信号值 SIGSEGV(11)
	// 相同值的出错码含义是“资源暂时不可用”，正好同义
	do_exit(SIGSEGV);
}

// 刷新页变换高速缓冲宏函数
// 为了提高地址转换的效率，CPU 将最近使用的页表数据存放在芯片中高速缓冲中
// 在修改过页表信息之后，就需要刷新该缓冲区，
// 这里使用重新加载页目录基址寄存器 cr3 的方法来进行刷新
// 下面 eax = 0，是页目录的基址
#define invalidate() __asm__("movl %%eax,%%cr3" ::"a"(0))

// 下面定义若需要改动，则需要与 head.s 等文件中的相关信息一起改变

// 内存低端（1MB）
#define LOW_MEM 0x100000

// 分页内存 15MB，主内存区最多15M
#define PAGING_MEMORY (15 * 1024 * 1024)

// 分页后的物理内存页数
#define PAGING_PAGES (PAGING_MEMORY >> 12)

// 指定内存地址映射为页号
#define MAP_NR(addr) (((addr)-LOW_MEM) >> 12)

// 页面被占用标志
#define USED 100

// 该宏用于判断给定地址，是否位于当前进程的代码段中
#define CODE_SPACE(addr) ((((addr) + 4095) & ~4095) < \
						  current->start_code + current->end_code)

// 全局变量，存放实际物理内存最高端地址
static long HIGH_MEMORY = 0;

// 复制1 页内存
#define copy_page(from, to) \
	__asm__("cld \n rep \n movsl\n" ::"S"(from), "D"(to), "c"(1024))

// 内存映射字节图(1 字节代表 1 页内存)，每个页面对应的字节用于标志页面当前被引用（占用）次数
static unsigned char mem_map[PAGING_PAGES] = {
	0,
};

// 获取首个(实际上是最后 1 个)空闲页面，并标记为已使用
// 如果没有空闲页面，就返回 0

// 取空闲页面，如果已经没有可用内存了，则返回 0
// 输入：
// %1(ax=0) - 0；
// %2(LOW_MEM)；
// %3(cx=PAGING PAGES)；
// %4(edi=mem_map+PAGING_PAGES-1)
// 输出：
// 返回%0(ax=页面起始地址)
// 上面%4 寄存器实际指向 mem_map[] 内存字节图的最后一个字节
// 本函数从字节图末端开始向前扫描所有页面标志（页面总数为PAGING_PAGES）
// 若有页面空闲（其内存映像字节为0）则返回页面地址。
// 注意！本函数只是指出在主内存区的一页空闲页面
// 但并没有映射到某个进程的线性地址去
// 后面的 put_page() 函数就是用来作映射的
unsigned long get_free_page(void)
{
	register unsigned long __res asm("ax");

	__asm__("std \n\t repne \n\t scasb\n\t"
			"jne 1f\n\t"
			"movb $1,1(%%edi)\n\t"
			"sall $12,%%ecx\n\t"
			"addl %2,%%ecx\n\t"
			"movl %%ecx,%%edx\n\t"
			"movl $1024,%%ecx\n\t"
			"leal 4092(%%edx),%%edi\n\t"
			"rep \n\t stosl\n\t"
			" movl %%edx,%%eax\n"
			"1: cld"
			: "=a"(__res)
			: "0"(0), "i"(LOW_MEM), "c"(PAGING_PAGES),
			  "D"(mem_map + PAGING_PAGES - 1));
	return __res;
}

// 释放物理地址 addr 开始的一页内存，用于函数 free_page_tables()
void free_page(unsigned long addr)
{
	// 如果物理地址 addr 小于内存低端（1MB），则返回
	if (addr < LOW_MEM)
		return;

	// 如果物理地址 addr>= 内存最高端，则显示出错信息
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page");

	// 物理地址减去低端内存位置，再除以4KB，得页面号
	addr -= LOW_MEM;
	addr >>= 12;

	// 如果对应内存页面映射字节不等于 0，则减 1 返回
	if (mem_map[addr]--)
		return;

	// 否则置对应页面映射字节为 0，并显示出错信息，死机
	mem_map[addr] = 0;
	panic("trying to free free page");
}

// 下面函数释放页表连续的内存块，exit() 需要该函数
// 与 copy_page_tables() 类似，该函数仅处理 4Mb 的内存块

// 根据指定的线性地址和限长（页表个数），释放对应内存页表所指定的内存块并置表项空闲
// 页目录位于物理地址 0 开始处，共 1024 项，占 4K 字节，每个目录项指定一个页表
// 页表从物理地址 0x1000 处开始（紧接着目录空间），每个页表有 1024 项，也占 4K 内存
// 每个页表项对应一页物理内存（4K），目录项和页表项的大小均为 4 个字节
// 参数：from - 起始基地址；size - 释放的长度
int free_page_tables(unsigned long from, unsigned long size)
{
	unsigned long *pg_table;
	unsigned long *dir, nr;

	// 要释放内存块的地址需以 4M 为边界
	if (from & 0x3fffff)
		panic("free_page_tables called with wrong alignment");

	// 出错，试图释放内核和缓冲所占空间
	if (!from)
		panic("Trying to free up swapper memory space");

	// 计算所占页目录项数(4M 的进位整数倍)，也即所占页表数
	size = (size + 0x3fffff) >> 22;

	// 下面一句计算起始目录项，对应的目录项号=from>>22
	// 因每项占 4 字节，并且由于页目录是从物理地址 0 开始
	// 因此实际的目录项指针=目录项号<<2，也即(from>>20)
	// 按位与 0xffc 确保目录项指针范围有效
	dir = (unsigned long *)((from >> 20) & 0xffc); /* _pg_dir = 0 */

	// size 现在是需要被释放内存的目录项数
	for (; size-- > 0; dir++)
	{
		// 如果该目录项无效(P 位=0)，则继续
		// 目录项的位0(P 位)表示对应页表是否存在
		if (!(1 & *dir))
			continue;

		// 取目录项中页表地址
		pg_table = (unsigned long *)(0xfffff000 & *dir);

		// 每个页表有 1024 个页项
		for (nr = 0; nr < 1024; nr++)
		{
			// 若该页表项有效(P 位=1)，则释放对应内存页
			if (1 & *pg_table)
				free_page(0xfffff000 & *pg_table);

			// 该页表项内容清
			*pg_table = 0;

			// 指向页表中下一项
			pg_table++;
		}

		// 释放该页表所占内存页面，
		// 但由于页表在物理地 址1M 以内，所以这句什么都不做
		free_page(0xfffff000 & *dir);

		// 对相应页表的目录项清零
		*dir = 0;
	}

	// 刷新页变换高速缓冲
	invalidate();
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */

// 好了，下面是内存管理 mm 中最为复杂的程序之一
// 它通过只复制内存页面来拷贝一定范围内线性地址中的内容
// 希望代码中没有错误，因为我不想再调试这块代码了 😊，[可见 Linus 也深受其害]。

// 注意！我们并不是仅复制任何内存块
// 内存块的地址需要是 4Mb 的倍数（正好一个页目录项对应的内存大小）
// 因为这样处理可使函数很简单。不管怎样，它仅被 fork() 使用

// 再次注意！！当 from==0 时，是在为第一次 fork() 调用复制内核空间
// 此时我们不想复制整个页目录项对应的内存，因为这样做会导致内存严重的浪费
// 我们只复制头 160 个页面 - 对应 640kB，即使是复制这些页面也已经超出我们的需求，
// 但这不会占用更多的内存 - 在低 1Mb 内存范围内我们不执行写时复制操作，
// 所以这些页面可以与内核共享。因此这是 nr=xxxx 的特殊情况（nr 在程序中指页面数）

// 复制指定线性地址和长度（页表个数）内存对应的页目录项和页表
// 从而被复制的页目录和页表对应的原物理内存区被共享使用
// 复制指定地址和长度的内存对应的页目录项和页表项
// 需申请页面来存放新页表，原内存区被共享；
// 此后两个进程将共享内存区，直到有一个进程执行写操作时，才分配新的内存页（写时复制机制）
int copy_page_tables(unsigned long from, unsigned long to, long size)
{
	unsigned long *from_page_table;
	unsigned long *to_page_table;
	unsigned long this_page;
	unsigned long *from_dir, *to_dir;
	unsigned long nr;

	// 源地址和目的地址都需要是在 4Mb 的内存边界地址上，否则出错，死机
	if ((from & 0x3fffff) || (to & 0x3fffff))
		panic("copy_page_tables called with wrong alignment");

	// 取得源地址和目的地址的目录项指针 (from_dir 和 to_dir)
	from_dir = (unsigned long *)((from >> 20) & 0xffc); /* _pg_dir = 0 */
	to_dir = (unsigned long *)((to >> 20) & 0xffc);

	// 计算要复制的内存块占用的页表数（也即目录项数）
	size = ((unsigned)(size + 0x3fffff)) >> 22;

	// 下面开始对每个占用的页表依次进行复制操作
	for (; size-- > 0; from_dir++, to_dir++)
	{
		// 如果目的目录项指定的页表已经存在(P=1)，则出错，死机
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");

		// 如果此源目录项未被使用，则不用复制对应页表，跳过
		if (!(1 & *from_dir))
			continue;

		// 取当前源目录项中页表的地址 -> from_page_table
		from_page_table = (unsigned long *)(0xfffff000 & *from_dir);

		// 为目的页表取一页空闲内存，如果返回是 0 则说明没有申请到空闲内存页面，返回值 =-1，退出
		if (!(to_page_table = (unsigned long *)get_free_page()))
			return -1; /* Out of memory, see freeing */

		// 设置目的目录项信息，7 是标志信息，表示(Usr, R/W, Present)
		*to_dir = ((unsigned long)to_page_table) | 7;

		// 针对当前处理的页表，设置需复制的页面数
		// 如果是在内核空间，则仅需复制头 160 页（640KB），
		// 否则需要复制一个页表中的所有 1024 页面
		nr = (from == 0) ? 0xA0 : 1024;

		// 对于当前页表，开始复制指定数目 nr 个内存页面
		for (; nr-- > 0; from_page_table++, to_page_table++)
		{
			// 取源页表项内容
			this_page = *from_page_table;

			// 如果当前源页面没有使用，则不用复制
			if (!(1 & this_page))
				continue;

			// 复位页表项中 R/W 标志(置0)
			// 如果U/S 位是 0，则R/W 就没有作用
			// 如果U/S 是1，而R/W 是0，那么运行在用户层的代码就只能读页面
			// 如果U/S 和R/W 都置位，则就有写的权限
			this_page &= ~2;

			// 将该页表项复制到目的页表中
			*to_page_table = this_page;

			// 如果该页表项所指页面的地址在 1MB 以上
			// 则需要设置内存页面映射数组mem_map[]
			// 于是计算页面号，并以它为索引在页面映射数组相应项中增加引用次数
			// 而对于位于 1MB 以下的页面，说明是内核页面
			// 因此不需要对 mem_map[] 进行设置
			// 因为 mem_map[] 仅用于管理主内存区中的页面使用情况
			// 因此，对于内核移动到任务 0 中并且调用 fork() 创建任务 1 时（用于运行init()）
			// 由于此时复制的页面还仍然都在内核代码区域，因此以下判断中的语句不会执行
			// 只有当调用 fork() 的父进程代码处于主内存区（页面位置大于1MB）时才会执行
			// 这种情况需要在进程调用了 execve()
			// 装载并执行了新程序代码时才会出现
			if (this_page > LOW_MEM)
			{
				// 下面这句的含义是令源页表项所指内存页也为只读
				// 因为现在开始有两个进程共用内存区了
				// 若其中一个内存需要进行写操作，则可以通过页异常的写保护处理
				// 为执行写操作的进程分配一页新的空闲页面，也即进行写时复制的操作

				// 令源页表项也只读
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	// 刷新页变换高速缓冲
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */

// 下面函数将一内存页面放置在指定地址处
// 它返回页面的物理地址，如果内存不够(在访问页表或页面时)，则返回 0

// 把物理内存页面映射到指定的线性地址处
// 主要工作是在页目录和页表中设置指定页面的信息，若成功则返回页面地址
// 在缺页异常的 C 函数 do_no_page() 中会调用此函数
// 对于缺页引起的异常，由于任何缺页缘故而对页表作修改时，
// 并不需要刷新CPU 的页变换缓冲（或称 Translation Lookaside Buffer，TLB）
// 即使页表项中标志 P 被从 0 修改成 1，因为无效页项不会被缓冲
// 因此当修改了一个无效的页表项时不需要刷新，在此就表现为不用调用 invalidate() 函数
unsigned long put_page(unsigned long page, unsigned long address)
{
	unsigned long tmp, *page_table;

	// 注意!!! 这里使用了页目录基址 pg_dir=0 的条件

	// 如果申请的页面位置低于 LOW_MEM(1Mb)
	// 或超出系统实际含有内存高端 HIGH_MEMORY，则发出警告
	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n", page, address);

	// 如果申请的页面在内存页面映射字节图中没有置位，则显示警告信息
	if (mem_map[(page - LOW_MEM) >> 12] != 1)
		printk("mem_map disagrees with %p at %p\n", page, address);

	// 计算指定地址在页目录表中对应的目录项指针
	page_table = (unsigned long *)((address >> 20) & 0xffc);

	// 如果该目录项有效(P=1)(也即指定的页表在内存中)，则从中取得指定页表的地址 -> page_table
	if ((*page_table) & 1)
		page_table = (unsigned long *)(0xfffff000 & *page_table);
	else
	{
		// 否则，申请空闲页面给页表使用，并在对应目录项中置相应标志 7（User, U/S, R/W）
		// 然后将该页表的地址 -> page_table
		if (!(tmp = get_free_page()))
			return 0;
		*page_table = tmp | 7;
		page_table = (unsigned long *)tmp;
	}

	// 在页表中设置指定地址的物理内存页面的页表项内容，每个页表共可有 1024 项(0x3ff)
	page_table[(address >> 12) & 0x3ff] = page | 7;

	// 不需要刷新页变换高速缓冲
	return page;
}

// 取消写保护页面函数，用于页异常中断过程中写保护异常的处理（写时复制）
// 输入参数为页表项指针
// [ un_wp_page 意思是取消页面的写保护：Un-Write Protected ]
void un_wp_page(unsigned long *table_entry)
{
	unsigned long old_page, new_page;

	// 取指定页表项内物理页面位置（地址）
	old_page = 0xfffff000 & *table_entry;

	// 如果原页面地址大于内存低端 LOW_MEM(1Mb)
	// 并且其在页面映射字节图数组中值为1（表示仅被引用1 次，页面没有被共享）
	// 则在该页面的页表项中置 R/W 标志（可写）
	// 并刷新页变换高速缓冲，然后返回
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)] == 1)
	{
		*table_entry |= 2;
		invalidate();
		return;
	}

	// 否则，在主内存区内申请一页空闲页面
	if (!(new_page = get_free_page()))
		// 内存不够处理
		oom();

	// 如果原页面大于内存低端（则意味着 mem_map[]>1，页面是共享的）
	// 则将原页面的页面映射数组值递减 1
	// 然后将指定页表项内容更新为新页面的地址，并置可读写等标志(U/S, R/W, P)
	// 刷新页变换高速缓冲。最后将原页面内容复制到新页面
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;
	*table_entry = new_page | 7;
	invalidate();
	copy_page(old_page, new_page);
}

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */

// 当用户试图往一个共享页面上写时，该函数处理已存在的内存页面，（写时复制）
// 它是通过将页面复制到一个新地址上并递减原页面的共享页面计数值实现的
// 如果它在代码空间，我们就以段错误信息退出

// 页异常中断处理调用的 C 函数，写共享页面处理函数，在 page.s 程序中被调用
// 参数 error_code 是由 CPU 自动产生，address 是页面线性地址
// 写共享页面时，需复制页面（写时复制）
void do_wp_page(unsigned long error_code, unsigned long address)
{
#if 0
	// 我们现在还不能这样做：因为 estdio 库会在代码空间执行写操作 
	// 真是太愚蠢了。我真想从 GNU 得到 libc.a 库。

	// 如果地址位于代码空间，则终止执行程序
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	// 处理取消页面保护，参数指定页面在页表中的页表项指针，其计算方法是：
	// ((address>>10) & 0xffc)：计算指定地址的页面在页表中的偏移地址；
	// (0xfffff000 & *((address>>20) &0xffc))：取目录项中页表的地址值；
	// 其中((address>>20) &0xffc)计算页面所在页表的目录项指针；
	// 两者相加即得指定地址对应页面的页表项指针，这里对共享的页面进行复制
	un_wp_page((unsigned long *)(((address >> 10) & 0xffc) + (0xfffff000 &
															  *((unsigned long *)((address >> 20) & 0xffc)))));
}

// 写页面验证
// 若页面不可写，则复制页面
void write_verify(unsigned long address)
{
	unsigned long page;

	// 判断指定地址所对应页目录项的页表是否存在(P)，若不存在 (P=0) 则返回
	if (!((page = *((unsigned long *)((address >> 20) & 0xffc))) & 1))
		return;

	// 取页表的地址，加上指定地址的页面在页表中的页表项偏移值，得对应物理页面的页表项指针
	page &= 0xfffff000;
	page += ((address >> 10) & 0xffc);

	// 如果该页面不可写(标志 R/W 没有置位)，则执行共享检验和复制页面操作（写时复制）
	if ((3 & *(unsigned long *)page) == 1)
		un_wp_page((unsigned long *)page);
	return;
}

// 取得一页空闲内存并映射到指定线性地址处
// 与 get_free_page() 不同
// get_free_page() 仅是申请取得了主内存区的一页物理内存
// 而该函数不仅是获取到一页物理内存页面，还进一步调用 put_page()
// 将物理页面映射到指定的线性地址处
void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	// 若不能取得一空闲页面，或者不能将页面放置到指定地址处，则显示内存不够的信息
	if (!(tmp = get_free_page()) || !put_page(tmp, address))
	{
		// 即使执行 get_free_page() 返回 0 也无所谓
		// 因为 put_page() 中还会对此情况再次申请空闲物理页面的
		free_page(tmp);
		oom();
	}
}

// try_to_share() 在任务 p 中检查位于地址 address 处的页面
// 看页面是否存在，是否干净，如果是干净的话，就与当前任务共享

// 注意！这里我们已假定 p != 当前任务，并且它们共享同一个执行程序

// 尝试对进程指定地址处的页面进行共享操作
// 同时还验证指定的地址处是否已经申请了页面，若是则出错，死机
// 返回 1-成功，0-失败
static int try_to_share(unsigned long address, struct task_struct *p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	// 求指定内存地址的页目录项
	from_page = to_page = ((address >> 20) & 0xffc);

	// 计算进程 p 的代码起始地址所对应的页目录项
	from_page += ((p->start_code >> 20) & 0xffc);

	// 计算当前进程中代码起始地址所对应的页目录项
	to_page += ((current->start_code >> 20) & 0xffc);

	// 在from 处是否存在页目录？

	// 对 p 进程页面进行操作，取页目录项内容
	// 如果该目录项无效(P=0)，则返回。否则取该目录项对应页表地址 -> from
	from = *(unsigned long *)from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;

	// 计算地址对应的页表项指针值，并取出该页表项内容 -> phys_addr
	from_page = from + ((address >> 10) & 0xffc);
	phys_addr = *(unsigned long *)from_page;

	// 页面干净并且存在吗？
	// 0x41 对应页表项中的 Dirty 和 Present 标志 (0b_0100_0001)
	// 如果页面不干净或无效则返回
	if ((phys_addr & 0x41) != 0x01)
		return 0;

	// 取页面的地址 -> phys_addr
	// 如果该页面地址不存在或小于内存低端(1M)也返回退出
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;

	// 对当前进程页面进行操作
	// 取页目录项内容 -> to
	// 如果该目录项无效(P=0)，则取空闲页面，并更新 to_page 所指的目录项
	to = *(unsigned long *)to_page;
	if (!(to & 1))
	{
		if ((to = get_free_page()))
			*(unsigned long *)to_page = to | 7;
		else
			oom();
	}

	// 取对应页表地址 -> to，页表项地址 -> to_page
	// 如果对应的页面已经存在，则出错，死机
	to &= 0xfffff000;
	to_page = to + ((address >> 10) & 0xffc);
	if (1 & *(unsigned long *)to_page)
		panic("try_to_share: to_page already exists");

	// 对它们进行共享处理：写保护
	// 对 p 进程中页面置写保护标志(置 R/W=0 只读)
	// 并且当前进程中的对应页表项指向它
	*(unsigned long *)from_page &= ~2;
	*(unsigned long *)to_page = *(unsigned long *)from_page;

	// 刷新页变换高速缓冲
	invalidate();

	// 计算所操作页面的页面号，并将对应页面映射数组项中的引用递增 1
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */

// share_page() 试图找到一个进程，它可以与当前进程共享页面
// 参数 address 是当前数据空间中期望共享的某页面地址

// 首先我们通过检测 executable->i_count 来查证是否可行
// 如果有其它任务已共享该 inode，则它应该大于 1

// 共享页面，在缺页处理时看看能否共享页面
// 返回 1 - 成功，0 - 失败
static int share_page(unsigned long address)
{
	struct task_struct **p;

	// 如果是不可执行的，则返回，excutable 是执行进程的内存 i 节点结构
	if (!current->executable)
		return 0;

	// 如果只能单独执行(executable->i_count=1)，也退出
	if (current->executable->i_count < 2)
		return 0;

	// 搜索任务数组中所有任务
	// 寻找与当前进程可共享页面的进程
	// 并尝试对指定地址的页面进行共享
	for (p = &LAST_TASK; p > &FIRST_TASK; --p)
	{
		// 如果该任务项空闲，则继续寻找
		if (!*p)
			continue;

		// 如果就是当前任务，也继续寻找
		if (current == *p)
			continue;

		// 如果 executable 不等，也继续
		if ((*p)->executable != current->executable)
			continue;

		// 尝试共享页面
		if (try_to_share(address, *p))
			return 1;
	}
	return 0;
}

// 页异常中断处理调用的函数，处理缺页异常情况，在 page.s 程序中被调用
// 参数 error_code 是由 CPU 自动产生，address 是页面线性地址
void do_no_page(unsigned long error_code, unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block, i;

	// 页面地址
	address &= 0xfffff000;

	// 首先算出指定线性地址 在进程空间中 相对于进程基址的偏移长度值
	tmp = address - current->start_code;

	// 若当前进程的 executable 空，或者指定地址超出代码 + 数据长度
	// 则申请一页物理内存，并映射影射到指定的线性地址处
	// executable 是进程的 i 节点结构。该值为 0，表明进程刚开始设置，需要内存；
	// 而指定的线性地址超出代码加数据长度，表明进程在申请新的内存空间，也需要给予
	// 因此就直接调用 get_empty_page() 函数，申请一页物理内存并映射到指定线性地址处即可
	// start_code 是进程代码段地址，end_data 是代码加数据长度
	// 对于 Linux 内核，它的代码段和数据段是起始基址是相同的
	if (!current->executable || tmp >= current->end_data)
	{
		get_empty_page(address);
		return;
	}

	// 如果尝试共享页面成功，则退出
	if (share_page(tmp))
		return;

	// 取空闲页面，如果内存不够了，则显示内存不够，终止进程
	if (!(page = get_free_page()))
		oom();

	// 记住，（程序）头要使用 1 个数据块
	// 首先计算缺页所在的数据块项，BLOCK_SIZE = 1024 字节，因此一页内存需要 4 个数据块
	block = 1 + tmp / BLOCK_SIZE;

	// 根据 i 节点信息，取数据块在设备上的对应的逻辑块号
	for (i = 0; i < 4; block++, i++)
		nr[i] = bmap(current->executable, block);

	// 读设备上一个页面的数据（4 个逻辑块）到指定物理地址 page 处
	bread_page(page, current->executable->i_dev, nr);

	// 在增加了一页内存后，该页内存的部分可能会超过进程的 end_data 位置
	// 下面的循环即是对物理页面超出的部分进行清零处理
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0)
	{
		tmp--;
		*(char *)tmp = 0;
	}

	// 如果把物理页面映射到指定线性地址的操作成功，就返回
	// 否则就释放内存页，显示内存不够
	if (put_page(page, address))
		return;
	free_page(page);
	oom();
}

// 物理内存初始化。
// 参数：
// start_mem / 可用作分页处理的物理内存起始位置（已去除 RAMDISK 所占内存空间等）
// end_mem / 实际物理内存最大地址
// 在该版的 Linux 内核中，最多能使用 16Mb 的内存
// 大于 16Mb 的内存将不于考虑，弃置不用
// 0 - 1Mb 内存空间用于内核系统（其实是0-640Kb）
void mem_init(long start_mem, long end_mem)
{
	int i;

	// 设置内存最高端
	HIGH_MEMORY = end_mem;

	// 首先置所有页面为已占用 (USED=100) 状态
	for (i = 0; i < PAGING_PAGES; i++)
		// 即将页面映射数组全置成 USED
		mem_map[i] = USED;

	// 然后计算可使用起始内存的页面号
	i = MAP_NR(start_mem);

	// 再计算可分页处理的内存块大小
	end_mem -= start_mem;

	// 从而计算出可用于分页处理的页面数
	end_mem >>= 12;

	// 最后将这些可用页面对应的页面映射数组清零
	while (end_mem-- > 0)
		mem_map[i++] = 0;
}

// 计算内存空闲页面数并显示
void calc_mem(void)
{
	int i, j, k, free = 0;
	long *pg_tbl;

	// 扫描内存页面映射数组 mem_map[]，获取空闲页面数并显示
	for (i = 0; i < PAGING_PAGES; i++)
		if (!mem_map[i])
			free++;
	printk("%d pages free (of %d)\n\r", free, PAGING_PAGES);

	// 扫描所有页目录项（除 0,1 项），如果页目录项有效，则统计对应页表中有效页面数，并显示
	for (i = 2; i < 1024; i++)
	{
		if (1 & pg_dir[i])
		{
			pg_tbl = (long *)(0xfffff000 & pg_dir[i]);
			for (j = k = 0; j < 1024; j++)
				if (pg_tbl[j] & 1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n", i, k);
		}
	}
}
