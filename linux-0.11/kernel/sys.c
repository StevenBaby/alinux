/*
 *  linux/kernel/sys.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 错误号头文件。包含系统中各种出错号
#include <errno.h>

// 调度程序头文件，定义了任务结构 task_struct、初始任务 0 的数据，
// 还有一些有关描述符参数设置和获取的内联汇编函数宏语句
#include <linux/sched.h>

// tty 头文件，定义了有关 tty_io，串行通信方面的参数、常数
#include <linux/tty.h>

// 内核头文件 含有一些内核常用函数的原形定义
#include <linux/kernel.h>

// 段操作头文件 定义了有关段寄存器操作的嵌入式汇编函数
#include <asm/segment.h>

// 定义了进程中运行时间的结构 tms 以及 times() 函数原型
#include <sys/times.h>

// 系统名称结构头文件
#include <sys/utsname.h>

// 返回日期和时间
int sys_ftime()
{
	return -ENOSYS;
}

int sys_break()
{
	return -ENOSYS;
}

// 用于当前进程对子进程进行调试
int sys_ptrace()
{
	return -ENOSYS;
}

// 改变并打印终端行设置
int sys_stty()
{
	return -ENOSYS;
}

// 取终端行设置信息
int sys_gtty()
{
	return -ENOSYS;
}

// 修改文件名
int sys_rename()
{
	return -ENOSYS;
}

int sys_prof()
{
	return -ENOSYS;
}

// 设置当前任务的实际以及/或者有效组ID（gid）
// 如果任务没有超级用户特权，那么只能互换其实际组 ID 和有效组 ID
// 如果任务具有超级用户特权，就能任意设置有效的和实际的组 ID
// 保留的gid（saved gid）被设置成与有效 gid 同值
int sys_setregid(int rgid, int egid)
{
	if (rgid > 0)
	{
		if ((current->gid == rgid) ||
			suser())
			current->gid = rgid;
		else
			return (-EPERM);
	}
	if (egid > 0)
	{
		if ((current->gid == egid) ||
			(current->egid == egid) ||
			(current->sgid == egid) ||
			suser())
			current->egid = egid;
		else
			return (-EPERM);
	}
	return 0;
}

// 设置进程组号(gid)，如果任务没有超级用户特权
// 它可以使用 setgid() 将其有效 gid（effective gid）
// 设置为成其保留 gid(saved gid) 或其实际 gid(real gid)
// 如果任务有超级用户特权，则实际 gid、有效 gid 和保留 gid
// 都被设置成参数指定的 gid
int sys_setgid(int gid)
{
	return (sys_setregid(gid, gid));
}

// 打开或关闭进程计帐功能
int sys_acct()
{
	return -ENOSYS;
}

// 映射任意物理内存到进程的虚拟地址空间
int sys_phys()
{
	return -ENOSYS;
}

int sys_lock()
{
	return -ENOSYS;
}

int sys_mpx()
{
	return -ENOSYS;
}

int sys_ulimit()
{
	return -ENOSYS;
}

// 返回从 1970 年1 月1 日00:00:00 GMT 开始计时的时间值（秒）
// 如果 tloc 不为 null，则时间值也存储在那里
int sys_time(long *tloc)
{
	int i;

	i = CURRENT_TIME;
	if (tloc)
	{
		// 验证内存容量是否够
		verify_area(tloc, 4);

		// 也放入用户数据段 tloc 处
		put_fs_long(i, (unsigned long *)tloc);
	}
	return i;
}

// 无特权的用户可以见实际用户标识符(real uid)
// 改成有效用户标识符(effective uid)，反之亦然

// 设置任务的实际以及/或者有效用户ID（uid）
// 如果任务没有超级用户特权，那么只能互换其实际用户 ID 和有效用户 ID
// 如果任务具有超级用户特权，就能任意设置有效的和实际的用户 ID
// 保留的 uid (saved uid) 被设置成与有效 uid 同值
int sys_setreuid(int ruid, int euid)
{
	int old_ruid = current->uid;

	if (ruid > 0)
	{
		if ((current->euid == ruid) ||
			(old_ruid == ruid) ||
			suser())
			current->uid = ruid;
		else
			return (-EPERM);
	}
	if (euid > 0)
	{
		if ((old_ruid == euid) ||
			(current->euid == euid) ||
			suser())
			current->euid = euid;
		else
		{
			current->uid = old_ruid;
			return (-EPERM);
		}
	}
	return 0;
}

// 设置任务用户号(uid) 如果任务没有超级用户特权，
// 它可以使用 setuid() 将其有效 uid（effective uid）
// 设置成其保留 uid(saved uid) 或其实际 uid(real uid)
// 如果任务有超级用户特权，则实际uid、有效uid 和保留uid 都被设置成参数指定的 uid
int sys_setuid(int uid)
{
	return (sys_setreuid(uid, uid));
}

// 设置系统时间和日期，参数 tptr 是从 1970 年 1 月 1 日 00:00:00 GMT 开始计时的时间值（秒）
// 调用进程必须具有超级用户权限
int sys_stime(long *tptr)
{
	if (!suser())
		return -EPERM;
	startup_time = get_fs_long((unsigned long *)tptr) - jiffies / HZ;
	return 0;
}

// 获取当前任务时间。tms 结构中包括用户时间、系统时间、子进程用户时间、子进程系统时间
int sys_times(struct tms *tbuf)
{
	if (tbuf)
	{
		verify_area(tbuf, sizeof *tbuf);
		put_fs_long(current->utime, (unsigned long *)&tbuf->tms_utime);
		put_fs_long(current->stime, (unsigned long *)&tbuf->tms_stime);
		put_fs_long(current->cutime, (unsigned long *)&tbuf->tms_cutime);
		put_fs_long(current->cstime, (unsigned long *)&tbuf->tms_cstime);
	}
	return jiffies;
}

// 当参数 end_data_seg 数值合理，并且系统确实有足够的内存，
// 而且进程没有超越其最大数据段大小时，
// 该函数设置数据段末尾为 end_data_seg 指定的值，
// 该值必须大于代码结尾并且要小于堆栈结尾 16KB
// 返回值是数据段的新结尾值（如果返回值与要求值不同，则表明有错发生）
// 该函数并不被用户直接调用，而由 libc 库函数进行包装，并且返回值也不一样
int sys_brk(unsigned long end_data_seg)
{
	// 如果参数>代码结尾，并且小于堆栈 - 16KB
	if (end_data_seg >= current->end_code &&
		end_data_seg < current->start_stack - 16384)
		// 则设置新数据段结尾值
		current->brk = end_data_seg;

	// 返回进程当前的数据段结尾值
	return current->brk;
}

// 下面代码需要某些严格的检查，我只是没有胃口来做这些
// 我也不完全明白 sessions/pgrp 等，还是让了解它们的人来做吧

// 设置进程的进程组 ID 为pgid
// 如果参数 pid=0，则使用当前进程号
// 如果 pgid 为 0，则使用参数 pid 指定的进程的组 ID 作为 pgid
// 如果该函数用于将进程从一个进程组移到另一个进程组
// 则这两个进程组必须属于同一个会话 (session)
// 在这种情况下，参数 pgid 指定了要加入的现有进程组 ID，
// 此时该组的会话 ID 必须与将要加入进程的相同
int sys_setpgid(int pid, int pgid)
{
	int i;

	// 如果参数 pid=0，则使用当前进程号
	if (!pid)
		pid = current->pid;

	// 如果 pgid 为 0，则使用当前进程 pid 作为 pgid
	if (!pgid)
		// [??这里与POSIX 的描述有出入]
		pgid = current->pid;

	// 扫描任务数组，查找指定进程号的任务
	for (i = 0; i < NR_TASKS; i++)
		if (task[i] && task[i]->pid == pid)
		{
			// 如果该任务已经是首领，则返回出错
			if (task[i]->leader)
				return -EPERM;

			// 如果该任务的会话 ID 与当前进程的不同，则出错返回
			if (task[i]->session != current->session)
				return -EPERM;

			// 设置该任务的 pgrp
			task[i]->pgrp = pgid;
			return 0;
		}
	return -ESRCH;
}

// 返回当前进程的组号，与 getpgid(0) 等同
int sys_getpgrp(void)
{
	return current->pgrp;
}

// 创建一个会话(session)（即设置其leader=1）
// 并且设置其会话号=其组号=其进程号
// setsid -- SET Session ID
int sys_setsid(void)
{
	// 如果当前进程已是会话首领并且不是超级用户，则返回出错
	if (current->leader && !suser())
		return -EPERM;

	// 设置当前进程为新会话首领
	current->leader = 1;

	// 设置本进程session = pid
	current->session = current->pgrp = current->pid;

	// 表示当前进程没有控制终端
	current->tty = -1;

	// 返回会话ID
	return current->pgrp;
}

// 获取系统信息。其中 utsname 结构包含 5 个字段
// 分别是：
// 本版本操作系统的名称、
// 网络节点名称、
// 当前发行级别、
// 版本级别、
// 硬件类型名称
int sys_uname(struct utsname *name)
{
	static struct utsname thisname = {
		// 这里给出了结构中的信息，这种编码肯定会改变
		"linux .0", "nodename", "release ", "version ", "machine "};
	int i;

	// 如果存放信息的缓冲区指针为空，则返回出错
	if (!name)
		return -ERROR;

	// 验证缓冲区大小是否超限（超出已分配的内存等）
	verify_area(name, sizeof *name);

	// 将 utsname 中的信息逐字节复制到用户缓冲区中
	for (i = 0; i < sizeof *name; i++)
		put_fs_byte(((char *)&thisname)[i], i + (char *)name);
	return 0;
}

// 设置当前进程创建文件属性屏蔽码为 mask & 0777，并返回原屏蔽码
int sys_umask(int mask)
{
	int old = current->umask;

	current->umask = mask & 0777;
	return (old);
}
