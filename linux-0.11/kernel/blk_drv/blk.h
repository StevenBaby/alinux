#ifndef _BLK_H
#define _BLK_H

// 块设备的数量
#define NR_BLK_DEV 7

// 下面定义的NR_REQUEST 是请求队列中所包含的项数
//  注意，读操作仅使用这些项低端的2/3；读操作优先处理

// 32 项好象是一个合理的数字：已经足够从电梯算法中获得好处
// 但当缓冲区在队列中而锁住时又不显得是很大的数
// 64 就看上去太大了（当大量的写/同步操作运行时很容易引起长时间的暂停）
#define NR_REQUEST 32

// OK，下面是 request 结构的一个扩展形式
// 因而当实现以后，我们就可以在分页请求中使用同样的 request 结构
// 在分页处理中，bh 是 NULL，而 waiting 则用于等待读/写的完成

// 下面是请求队列中项的结构。其中如果dev=-1，则表示该项没有被使用
struct request
{
	int dev;					 // 使用的设备号
	int cmd;					 // 命令(READ 或 WRITE)
	int errors;					 //操作时产生的错误次数
	unsigned long sector;		 // 起始扇区。(1 块=2 扇区)
	unsigned long nr_sectors;	 // 读/写扇区数
	char *buffer;				 // 数据缓冲区
	struct task_struct *waiting; // 任务等待操作执行完成的地方
	struct buffer_head *bh;		 // 缓冲区头指针
	struct request *next;		 // 指向下一请求项
};

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */

// 下面的定义用于电梯算法：注意读操作总是在写操作之前进行
// 这是很自然的：读操作对时间的要求要比写操作严格得多

#define IN_ORDER(s1, s2)                                                            \
	((s1)->cmd < (s2)->cmd || ((s1)->cmd == (s2)->cmd &&                            \
							   ((s1)->dev < (s2)->dev || ((s1)->dev == (s2)->dev && \
														  (s1)->sector < (s2)->sector))))

// 块设备结构
struct blk_dev_struct
{
	// 请求操作的函数指针
	void (*request_fn)(void);

	// 请求信息结构
	struct request *current_request;
};

// 块设备表（数组），每种块设备占用一项
extern struct blk_dev_struct blk_dev[NR_BLK_DEV];

// 请求队列数组
extern struct request request[NR_REQUEST];

// 等待空闲请求的任务结构队列头指针
extern struct task_struct *wait_for_request;

// 在块设备驱动程序（如hd.c）要包含此头文件时
// 必须先定义驱动程序对应设备的主设备号
// 这样下面就能为包含本文件的驱动程序给出正确的宏定义
#ifdef MAJOR_NR

// 需要时加入条目，目前块设备仅支持硬盘和软盘（还有虚拟盘）

// RAM 盘的主设备号是 1，根据这里的定义可以推理内存块主设备号也为 1
#if (MAJOR_NR == 1)

// RAM 盘（内存虚拟盘）

// 设备名称ramdisk
#define DEVICE_NAME "ramdisk"

// 设备请求函数do_rd_request()
#define DEVICE_REQUEST do_rd_request

// 设备号(0--7)
#define DEVICE_NR(device) ((device)&7)

// 开启设备。虚拟盘无须开启和关闭
#define DEVICE_ON(device)

// 关闭设备
#define DEVICE_OFF(device)

// 软驱的主设备号是 2
#elif (MAJOR_NR == 2)
/* floppy */

// 设备名称 floppy
#define DEVICE_NAME "floppy"

// 设备中断处理程序 do_floppy()
#define DEVICE_INTR do_floppy

// 设备请求函数 do_fd_request()
#define DEVICE_REQUEST do_fd_request

// 设备号（0--3）
#define DEVICE_NR(device) ((device)&3)

// 开启设备函数 floppyon()
#define DEVICE_ON(device) floppy_on(DEVICE_NR(device))

// 关闭设备函数 floppyoff()
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

// 硬盘主设备号是 3
#elif (MAJOR_NR == 3)
/* harddisk */

// 硬盘名称harddisk
#define DEVICE_NAME "harddisk"

// 设备中断处理程序do_hd()
#define DEVICE_INTR do_hd

// 设备请求函数do_hd_request()
#define DEVICE_REQUEST do_hd_request

// 设备号（0--1）。每个硬盘可以有4 个分区
#define DEVICE_NR(device) (MINOR(device) / 5)

// 硬盘一直在工作，无须开启和关闭
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#else
// 未知块设备
#error "unknown blk device"

#endif

// CURRENT 是指定主设备号的当前请求结构
#define CURRENT (blk_dev[MAJOR_NR].current_request)

// CURRENT_DEV 是 CURRENT 的设备号
#define CURRENT_DEV DEVICE_NR(CURRENT->dev)

// 下面申明两个宏定义为函数指针
#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif
static void(DEVICE_REQUEST)(void);

// / 释放锁定的缓冲区（块）
static inline void unlock_buffer(struct buffer_head *bh)
{
	// 如果指定的缓冲区 bh 并没有被上锁，则显示警告信息
	if (!bh->b_lock)
		printk(DEVICE_NAME ": free buffer being unlocked\n");

	// 否则将该缓冲区解锁
	bh->b_lock = 0;

	// 唤醒等待该缓冲区的进程
	wake_up(&bh->b_wait);
}

// 结束请求处理
// 首先关闭指定块设备，然后检查此次读写缓冲区是否有效
// 如果有效则根据参数值设置缓冲区数据更新标志，并解锁该缓冲区
// 如果更新标志参数值是 0，表示此次请求项的操作已失败
// 因此显示相关块设备 IO 错误信息
// 最后，唤醒等待该请求项的进程以及等待空闲请求项出现的进程
// 释放并从请求链表中删除本请求项
static inline void end_request(int uptodate)
{
	DEVICE_OFF(CURRENT->dev); // 关闭设备

	if (CURRENT->bh) // CURRENT 为指定主设备号的当前请求结构
	{
		// 置更新标志
		CURRENT->bh->b_uptodate = uptodate;

		// 解锁缓冲区
		unlock_buffer(CURRENT->bh);
	}
	if (!uptodate) // 如果更新标志为 0 则显示设备错误信息
	{
		printk(DEVICE_NAME " I/O error\n\r");
		printk("dev %04x, block %d\n\r", CURRENT->dev,
			   CURRENT->bh->b_blocknr);
	}
	wake_up(&CURRENT->waiting); // 唤醒等待该请求项的进程
	wake_up(&wait_for_request); // 唤醒等待请求的进程
	CURRENT->dev = -1;			// 释放该请求项

	// 从请求链表中删除该请求项，并且当前请求项指针指向下一个请求项
	CURRENT = CURRENT->next;
}

// 定义初始化请求宏
#define INIT_REQUEST                                                                               \
	repeat:                                                                                        \
	if (!CURRENT)						 /* 如果当前请求结构指针为null 则返回 */     \
		return;							 /* 表示本设备目前已无需要处理的请求项 */ \
	if (MAJOR(CURRENT->dev) != MAJOR_NR) /* 如果当前设备的主设备号不对则死机 */    \
		panic(DEVICE_NAME ": request list destroyed");                                             \
	if (CURRENT->bh)                                                                               \
	{                                                                                              \
		if (!CURRENT->bh->b_lock) /* 如果在进行请求操作时缓冲区没锁定则死机 */  \
			panic(DEVICE_NAME ": block not locked");                                               \
	}

#endif

#endif
