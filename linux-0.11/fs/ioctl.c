/*
 *  linux/fs/ioctl.c
 *
 *  (C) 1991  Linus Torvalds
 */

/* #include <string.h>*/
#include <errno.h>
#include <sys/stat.h>

#include <linux/sched.h>

// 终端 ioctl
extern int tty_ioctl(int dev, int cmd, int arg);

// 定义输入输出控制(ioctl)函数指针
typedef int (*ioctl_ptr)(int dev, int cmd, int arg);

// 定义系统中设备种数
#define NRDEVS ((sizeof(ioctl_table)) / (sizeof(ioctl_ptr)))

// ioctl 操作函数指针表
static ioctl_ptr ioctl_table[] = {
	NULL,	   /* nodev */
	NULL,	   /* /dev/mem */
	NULL,	   /* /dev/fd */
	NULL,	   /* /dev/hd */
	tty_ioctl, /* /dev/ttyx */
	tty_ioctl, /* /dev/tty */
	NULL,	   /* /dev/lp */
	NULL};	   /* named pipes */

// 系统调用函数 - 输入输出控制函数
// 参数：fd - 文件描述符；cmd - 命令码；arg - 参数；
// 返回：成功则返回0，否则返回出错码
int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct file *filp;
	int dev, mode;

	// 如果文件描述符超出可打开的文件数，或者对应描述符的文件结构指针为空，则返回出错码，退出
	if (fd >= NR_OPEN || !(filp = current->filp[fd]))
		return -EBADF;

	// 取对应文件的属性，如果该文件不是字符文件，也不是块设备文件，则返回出错码，退出
	mode = filp->f_inode->i_mode;
	if (!S_ISCHR(mode) && !S_ISBLK(mode))
		return -EINVAL;

	// 从字符或块设备文件的 i 节点中取设备号，如果设备号大于系统现有的设备数，则返回出错号
	dev = filp->f_inode->i_zone[0];
	if (MAJOR(dev) >= NRDEVS)
		return -ENODEV;

	// 如果该设备在 ioctl 函数指针表中没有对应函数，则返回出错码
	if (!ioctl_table[MAJOR(dev)])
		return -ENOTTY;

	// 否则返回实际 ioctl 函数返回码，成功则返回0，否则返回出错码
	return ioctl_table[MAJOR(dev)](dev, cmd, arg);
}
