/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

// 该程序处理块设备的所有读/写操作

#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

// 请求结构中含有加载 nr 扇区数据到内存中的所有必须的信息
struct request request[NR_REQUEST];

// 是用于在请求数组没有空闲项时进程的临时等待处
struct task_struct *wait_for_request = NULL;

// blk_dev_struct 块设备结构是：
// do_request-address // 对应主设备号的请求处理程序指针
// current-request // 该设备的下一个请求

// 该数组使用主设备号作为索引
// 实际内容将在各种块设备驱动程序初始化时填入
// 例如，硬盘驱动程序进行初始化时
// 第一条语句即用于设置 blk_dev[3] 的内容
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{NULL, NULL}, // 0 - 无设备
	{NULL, NULL}, // 1 - 内存
	{NULL, NULL}, // 2 - 软驱设备
	{NULL, NULL}, // 3 - 硬盘设备
	{NULL, NULL}, // 4 - ttyx 设备
	{NULL, NULL}, // 5 - tty 设备
	{NULL, NULL}  // 6 - lp 打印机设备
};

// 锁定指定的缓冲区 bh
// 如果指定的缓冲区已经被其它任务锁定，则使自己睡眠（不可中断地等待）
// 直到被执行解锁缓冲区的任务明确地唤醒
static inline void lock_buffer(struct buffer_head *bh)
{
	cli(); // 关中断

	// 如果缓冲区已被锁定，则睡眠，直到缓冲区解锁
	while (bh->b_lock)
		sleep_on(&bh->b_wait);

	// 立刻锁定该缓冲区
	bh->b_lock = 1;
	sti(); // 开中断
}

// 释放（解锁）锁定的缓冲区
static inline void unlock_buffer(struct buffer_head *bh)
{
	// 如果该缓冲区并没有被锁定，则打印出错信息
	if (!bh->b_lock)
		printk("ll_rw_block.c: buffer not locked\n\r");

	// 清锁定标志
	bh->b_lock = 0;

	// 唤醒等待该缓冲区的任务
	wake_up(&bh->b_wait);
}

// add-request() 向链表中加入一项请求
// 它会关闭中断，这样就能安全地处理请求链表了

// 向链表中加入请求项，参数 dev 指定块设备，req 是请求项结构信息指针
static void add_request(struct blk_dev_struct *dev, struct request *req)
{
	struct request *tmp;

	req->next = NULL;
	cli(); // 关中断
	if (req->bh)
		req->bh->b_dirt = 0; // 清缓冲区 脏 标志

	// 如果 dev 的当前请求(current_request)子段为空，则表示目前该设备没有请求项
	// 本次是第 1 个请求项，因此可将块设备当前请求指针直接指向该请求项，并立刻执行相应设备的请求函数
	if (!(tmp = dev->current_request))
	{
		dev->current_request = req;

		// 开中断
		sti();

		// 执行设备请求函数，对于硬盘是 do_hd_request()
		(dev->request_fn)();
		return;
	}

	// 如果目前该设备已经有请求项在等待，则首先利用电梯算法搜索最佳插入位置
	// 然后将当前请求插入到请求链表中，电梯算法的作用是让磁盘磁头的移动距离最小，从而改善硬盘访问时间
	for (; tmp->next; tmp = tmp->next)
		if ((IN_ORDER(tmp, req) ||
			 !IN_ORDER(tmp, tmp->next)) &&
			IN_ORDER(req, tmp->next))
			break;
	req->next = tmp->next;
	tmp->next = req;
	sti();
}

// 创建请求项并插入请求队列
// 参数是：主设备号 major，命令 rw，存放数据的缓冲区头指针 bh
static void make_request(int major, int rw, struct buffer_head *bh)
{
	struct request *req;
	int rw_ahead;

	// WRITEA/READA 是一种特殊情况 - 它们并非必要，所以如果缓冲区已经上锁
	// 我们就不管它而退出，否则的话就执行一般的读/写操作

	// 这里'READ'和'WRITE'后面的'A'字符代表英文单词Ahead，表示提前预读/写数据块的意思
	// 对于命令是 READA/WRITEA 的情况，当指定的缓冲区正在使用，已被上锁时，就放弃预读/写请求
	// 否则就作为普通的 READ/WRITE 命令进行操作
	if ((rw_ahead = (rw == READA || rw == WRITEA)))
	{
		if (bh->b_lock)
			return;
		if (rw == READA)
			rw = READ;
		else
			rw = WRITE;
	}

	// 如果命令不是 READ 或 WRITE 则表示内核程序有错，显示出错信息并死机
	if (rw != READ && rw != WRITE)
		panic("Bad block dev command, must be R/W/RA/WA");

	// 锁定缓冲区，如果缓冲区已经上锁，则当前任务（进程）就会睡眠，直到被明确地唤醒
	lock_buffer(bh);

	// 如果命令是写并且缓冲区数据不脏（没有被修改过）
	// 或者命令是读并且缓冲区数据是更新过的
	// 则不用添加这个请求，将缓冲区解锁并退出
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate))
	{
		unlock_buffer(bh);
		return;
	}
repeat:
	// 我们不能让队列中全都是写请求项：我们需要为读请求保留一些空间
	// 读操作是优先的，请求队列的后三分之一空间是为读准备的

	// 请求项是从请求数组末尾开始搜索空项填入的
	// 根据上述要求，对于读命令请求，可以直接从队列末尾开始操作
	// 而写请求则只能从队列 2/3 处向队列头处搜索空项填入
	if (rw == READ)
		// 对于读请求，将队列指针指向队列尾部
		req = request + NR_REQUEST;
	else
		// 对于写请求，队列指针指向队列 2/3 处
		req = request + ((NR_REQUEST * 2) / 3);

	// 搜索一个空请求项
	// 从后向前搜索，当请求结构 request 的 dev 字段值 =-1 时，表示该项未被占用
	while (--req >= request)
		if (req->dev < 0)
			break;

	// 如果没有找到空闲项，则让该次新请求睡眠：需检查是否提前读/写
	// 如果没有一项是空闲的（此时request 数组指针已经搜索越过头部）
	// 则查看此次请求是否是提前读/写（READA 或WRITEA）
	// 如果是则放弃此次请求
	// 否则让本次请求睡眠（等待请求队列腾出空项），过一会再来搜索请求队列
	if (req < request)
	{
		// 如果是提前读/写请求，则解锁缓冲区，退出
		if (rw_ahead)
		{
			unlock_buffer(bh);
			return;
		}

		// 否则让本次请求睡眠，过会再查看请求队列
		sleep_on(&wait_for_request);
		goto repeat;
	}

	// 向空闲请求项中填写请求信息，并将其加入队列中
	// 程序执行到这里表示已找到一个空闲请求项

	// 设备号
	req->dev = bh->b_dev;

	// 命令(READ/WRITE)
	req->cmd = rw;

	// 操作时产生的错误次数
	req->errors = 0;

	// 起始扇区，块号转换成扇区号
	req->sector = bh->b_blocknr << 1;

	// 读写扇区数
	req->nr_sectors = 2;

	// 数据缓冲区
	req->buffer = bh->b_data;

	// 任务等待操作执行完成的地方
	req->waiting = NULL;

	// 缓冲块头指针
	req->bh = bh;

	// 指向下一请求项
	req->next = NULL;

	// 将请求项加入队列中
	add_request(major + blk_dev, req);
}

// 低层读写数据块函数，是块设备与系统其它部分的接口函数
// 该函数在 fs/buffer.c 中被调用
// 主要功能是创建块设备读写请求项，并插入到指定块设备请求队列中
// 实际的读写操作则是由设备的 request_fn() 函数完成
// 对于硬盘操作，该函数是 do_hd_request()；
// 对于软盘操作，该函数是 do_fd_request()；
// 对于虚拟盘则是 do_rd_request()；
// 另外，需要读/写块设备的信息已保存在缓冲块头结构中，如设备号、块号
// 参数：rw – READ、READA、WRITE 或 WRITEA 命令；bh – 数据缓冲块头指针
void ll_rw_block(int rw, struct buffer_head *bh)
{
	// 主设备号（对于硬盘是3）
	unsigned int major;

	// 如果设备的主设备号不存在或者该设备的读写操作函数不存在，则显示出错信息，并返回
	if ((major = MAJOR(bh->b_dev)) >= NR_BLK_DEV ||
		!(blk_dev[major].request_fn))
	{
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	// 创建请求项并插入请求队列
	make_request(major, rw, bh);
}


// 块设备初始化函数，由初始化程序 main.c 调用
// 初始化请求数组，将所有请求项置为空闲项(dev = -1)，有 32 项(NR_REQUEST = 32)
void blk_dev_init(void)
{
	int i;

	for (i = 0; i < NR_REQUEST; i++)
	{
		request[i].dev = -1;
		request[i].next = NULL;
	}
}
