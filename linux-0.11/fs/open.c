/*
 *  linux/fs/open.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* #include <string.h> */
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <asm/segment.h>

// 取文件系统信息系统调用函数
int sys_ustat(int dev, struct ustat *ubuf)
{
	return -ENOSYS;
}

// 设置文件访问和修改时间
// 参数 filename 是文件名，times 是访问和修改时间结构指针
// 如果 times 指针不为 NULL，则取 utimbuf 结构中的时间信息来设置文件的访问和修改时间
// 如果 times 指针是NULL，则取系统当前时间来设置指定文件的访问和修改时间域
int sys_utime(char *filename, struct utimbuf *times)
{
	struct m_inode *inode;
	long actime, modtime;

	// 根据文件名寻找对应的 i 节点，如果没有找到，则返回出错码
	if (!(inode = namei(filename)))
		return -ENOENT;

	// 如果访问和修改时间数据结构指针不为 NULL，则从结构中读取用户设置的时间值
	if (times)
	{
		actime = get_fs_long((unsigned long *)&times->actime);
		modtime = get_fs_long((unsigned long *)&times->modtime);
	}
	else
		// 否则将访问和修改时间置为当前时间
		actime = modtime = CURRENT_TIME;

	// 修改 i 节点中的访问时间字段和修改时间字段
	inode->i_atime = actime;
	inode->i_mtime = modtime;

	// 置 i 节点已修改标志，释放该节点，并返回 0
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

// 文件属性 XXX，我们该用真实用户 id 还是有效用户 id？BSD 系统使用了真实用户 id
// 以使该调用可以供 setuid 程序使用（注：POSIX 标准建议使用真实用户 ID）

// 检查对文件的访问权限
// 参数 filename 是文件名，mode 是屏蔽码，由 R_OK(4)、W_OK(2)、X_OK(1) 和 F_OK(0) 组成
// 如果请求访问允许的话，则返回 0，否则返回出错码
int sys_access(const char *filename, int mode)
{
	struct m_inode *inode;
	int res, i_mode;

	// 屏蔽码由低 3 位组成，因此清除所有高比特位
	mode &= 0007;

	// 如果文件名对应的 i 节点不存在，则返回出错码
	if (!(inode = namei(filename)))
		return -EACCES;

	// 取文件的属性码，并释放该 i 节点
	i_mode = res = inode->i_mode & 0777;
	iput(inode);

	// 如果当前进程是该文件的宿主，则取文件宿主属性
	if (current->uid == inode->i_uid)
		res >>= 6;

	// 否则如果当前进程是与该文件同属一组，则取文件组属性
	else if (current->gid == inode->i_gid)
		res >>= 6;

	// 如果文件属性具有查询的属性位，则访问许可，返回0
	if ((res & 0007 & mode) == mode)
		return 0;

	// XXX 我们最后才做下面的测试
	// 因为我们实际上需要交换有效用户 id 和 真实用户id（临时地）
	// 然后才调用 suser() 函数
	// 如果我们确实要调用 suser() 函数，则需要最后才被调用

	// 如果当前用户 id 为 0（超级用户）并且屏蔽码执行位是 0 或文件可以被任何人访问，则返回 0
	if ((!current->uid) &&
		(!(mode & 1) || (i_mode & 0111)))
		return 0;

	// 否则返回出错码
	return -EACCES;
}

// 改变当前工作目录系统调用函数
// 参数 filename 是目录名
// 操作成功则返回 0，否则返回出错码
int sys_chdir(const char *filename)
{
	struct m_inode *inode;

	// 如果文件名对应的 i 节点不存在，则返回出错码
	if (!(inode = namei(filename)))
		return -ENOENT;

	// 如果该 i 节点不是目录的 i 节点，则释放该节点，返回出错码
	if (!S_ISDIR(inode->i_mode))
	{
		iput(inode);
		return -ENOTDIR;
	}

	// 释放当前进程原工作目录 i 节点，并指向该新置的工作目录 i 节点。返回 0
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

// 改变根目录系统调用函数
// 将指定的路径名改为根目录'/'
// 如果操作成功则返回0，否则返回出错码
int sys_chroot(const char *filename)
{
	struct m_inode *inode;

	// 如果文件名对应的 i 节点不存在，则返回出错码
	if (!(inode = namei(filename)))
		return -ENOENT;

	// 如果该 i 节点不是目录的 i 节点，则释放该节点，返回出错码
	if (!S_ISDIR(inode->i_mode))
	{
		iput(inode);
		return -ENOTDIR;
	}

	// 释放当前进程的根目录 i 节点，并重置为这里指定目录名的 i 节点，返回 0
	iput(current->root);
	current->root = inode;
	return (0);
}

// 修改文件属性系统调用函数
// 参数 filename 是文件名，mode 是新的文件属性
// 若操作成功则返回 0，否则返回出错码
int sys_chmod(const char *filename, int mode)
{
	struct m_inode *inode;

	// 如果文件名对应的 i 节点不存在，则返回出错码
	if (!(inode = namei(filename)))
		return -ENOENT;

	// 如果当前进程的有效用户 id 不等于文件 i 节点的用户 id
	// 并且当前进程不是超级用户，则释放该文件 i 节点，返回出错码
	if ((current->euid != inode->i_uid) && !suser())
	{
		iput(inode);
		return -EACCES;
	}

	// 重新设置 i 节点的文件属性，并置该 i 节点已修改标志，释放该 i 节点，返回 0
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

// 修改文件宿主系统调用函数
// 参数 filename 是文件名，uid 是用户标识符(用户id)，gid 是组 id
// 若操作成功则返回 0，否则返回出错码
int sys_chown(const char *filename, int uid, int gid)
{
	struct m_inode *inode;

	// 如果文件名对应的 i 节点不存在，则返回出错码
	if (!(inode = namei(filename)))
		return -ENOENT;

	// 若当前进程不是超级用户，则释放该 i 节点，返回出错码
	if (!suser())
	{
		iput(inode);
		return -EACCES;
	}

	// 设置文件对应 i 节点的用户 id 和组 id
	// 并置 i 节点已经修改标志，释放该 i 节点，返回 0
	inode->i_uid = uid;
	inode->i_gid = gid;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

// 打开（或创建）文件系统调用函数
// 参数 filename 是文件名
// flag 是打开文件标志：
// 只读O_RDONLY
// 只写O_WRONLY
// 读写O_RDWR，
// 以及O_CREAT、O_EXCL、O_APPEND 等其它一些标志的组合，
// 若本函数创建了一个新文件，则 mode 用于指定使用文件的许可属性
// 这些属性有：
// S_IRWXU(文件宿主具有读、写和执行权限)
// S_IRUSR(用户具有读文件权限)
// S_IRWXG(组成员具有读、写和执行权限)等等
// 对于新创建的文件，这些属性只应用于将来对文件的访问
// 创建了只读文件的打开调用也将返回一个可读写的文件句柄
// 若操作成功则返回文件句柄(文件描述符)，否则返回出错码
int sys_open(const char *filename, int flag, int mode)
{
	struct m_inode *inode;
	struct file *f;
	int i, fd;

	// 将用户设置的模式与进程的模式屏蔽码相与，产生许可的文件模式
	mode &= 0777 & ~current->umask;

	// 搜索进程结构中文件结构指针数组，查找一个空闲项，若已经没有空闲项，则返回出错码
	for (fd = 0; fd < NR_OPEN; fd++)
		if (!current->filp[fd])
			break;
	if (fd >= NR_OPEN)
		return -EINVAL;

	// 设置执行时关闭文件句柄位图，复位对应比特位
	current->close_on_exec &= ~(1 << fd);

	// 令 f 指向文件表数组开始处
	// 搜索空闲文件结构项(句柄引用计数为0 的项)
	// 若已经没有空闲文件表结构项，则返回出错码
	f = 0 + file_table;
	for (i = 0; i < NR_FILE; i++, f++)
		if (!f->f_count)
			break;
	if (i >= NR_FILE)
		return -EINVAL;

	// 让进程的对应文件句柄的文件结构指针指向搜索到的文件结构，并令句柄引用计数递增 1
	(current->filp[fd] = f)->f_count++;

	// 调用函数执行打开操作，若返回值小于 0，则说明出错，释放刚申请到的文件结构，返回出错码
	if ((i = open_namei(filename, flag, mode, &inode)) < 0)
	{
		current->filp[fd] = NULL;
		f->f_count = 0;
		return i;
	}

	// ttys 有些特殊，ttyxx 主号==4，tty 主号==5
	// 如果是字符设备文件，那么如果设备号是 4 的话，则设置当前进程的 tty 号为该i 节点的子设备号
	// 并设置当前进程tty 对应的 tty 表项的父进程组号等于进程的父进程组号
	if (S_ISCHR(inode->i_mode))
	{
		if (MAJOR(inode->i_zone[0]) == 4)
		{
			if (current->leader && current->tty < 0)
			{
				current->tty = MINOR(inode->i_zone[0]);
				tty_table[current->tty].pgrp = current->pgrp;
			}
		}
		// 否则如果该字符文件设备号是 5 的话，若当前进程没有 tty，则说明出错
		// 释放 i 节点和申请到的文件结构，返回出错码
		else if (MAJOR(inode->i_zone[0]) == 5)
			if (current->tty < 0)
			{
				iput(inode);
				current->filp[fd] = NULL;
				f->f_count = 0;
				return -EPERM;
			}
	}

	// 同样对于块设备文件：需要检查盘片是否被更换

	// 如果打开的是块设备文件，则检查盘片是否更换
	// 若更换则需要是高速缓冲中对应该设备的所有缓冲块失效
	if (S_ISBLK(inode->i_mode))
		check_disk_change(inode->i_zone[0]);

	// 初始化文件结构，置文件结构属性和标志，置句柄引用计数为 1
	// 设置 i 节点字段，文件读写指针初始化为 0，返回文件句柄
	f->f_mode = inode->i_mode;
	f->f_flags = flag;
	f->f_count = 1;
	f->f_inode = inode;
	f->f_pos = 0;
	return (fd);
}

// 创建文件系统调用函数
// 参数 pathname 是路径名，mode 与上面的 sys_open() 函数相同
// 成功则返回文件句柄，否则返回出错码
int sys_creat(const char *pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_TRUNC, mode);
}

// 关闭文件系统调用函数
// 参数 fd 是文件句柄
// 成功则返回0，否则返回出错码
int sys_close(unsigned int fd)
{
	struct file *filp;

	// 若文件句柄值大于程序同时能打开的文件数，则返回出错码
	if (fd >= NR_OPEN)
		return -EINVAL;

	// 复位进程的执行时关闭文件句柄位图对应位
	current->close_on_exec &= ~(1 << fd);

	// 若该文件句柄对应的文件结构指针是 NULL，则返回出错码
	if (!(filp = current->filp[fd]))
		return -EINVAL;

	// 置该文件句柄的文件结构指针为 NULL
	current->filp[fd] = NULL;

	// 若在关闭文件之前，对应文件结构中的句柄引用计数已经为 0，则说明内核出错，死机
	if (filp->f_count == 0)
		panic("Close: file count is 0");

	// 否则将对应文件结构的句柄引用计数减 1，如果还不为 0，则返回 0（成功）
	// 若已等于 0，说明该文件已经没有句柄引用，则释放该文件 i 节点，返回 0
	if (--filp->f_count)
		return (0);
	iput(filp->f_inode);
	return (0);
}
