/*
 *  linux/tools/build.c
 *
 *  (C) 1991  Linus Torvalds
 */

// 该程序从三个不同的程序中创建磁盘映象文件：

// - bootsect：该文件的 8086 机器码最长为 510 字节，用于加载其它程序
// - setup：该文件的 8086 机器码最长为 4 个磁盘扇区，用于设置系统参数
// - system：实际系统的 80386 代码
// 该程序首先检查所有程序模块的类型是否正确，并将检查结果在终端上显示出来，
// 然后删除模块头部并扩充大正确的长度。该程序也会将一些系统数据写到stderr。

// tytso 对该程序作了修改，以允许指定根文件设备

// 使用其中的 fprintf()
#include <stdio.h>

// 字符串操作
#include <string.h>

// 含有 exit()
#include <stdlib.h>

// 供 unistd.h 使用
#include <sys/types.h>

// 文件状态信息结构
#include <sys/stat.h>

// 含有 read() / write()
#include <unistd.h>

// 文件操作模式符号常数
#include <fcntl.h>

#define MAJOR(a) (((unsigned)(a)) >> 8)
#define MINOR(a) ((a)&0xff)

// minix 二进制模块头部长度为32 字节
#define MINIX_HEADER 32

// GCC 头部信息长度为 1024 字节
#define GCC_HEADER 1024
// 注：当前的 gcc 生成的 ELF 文件头部变成了 4K 也就是一页
// 由于前面使用了 objcopy 生成了内存映像，所以这个头用不了了
// 下面用到这个的地方只能注释掉

// kernel 文件最长节数 (字节数为 SYS_SIZE * 16 = 192 KB)
// 目前生成的 kernel 文件 147 KB
// system 文件由于加入了调试信息，以及一些运行时不需要的信息，稍微大一点
#define SYS_SIZE 0x3000

// 默认根设备主设备号 - 3（硬盘）
#define DEFAULT_MAJOR_ROOT 3

// 默认根设备次设备号 (第 1 个盘的第 1 个分区)
#define DEFAULT_MINOR_ROOT 1

// 下面指定setup 模块占的最大扇区数：不要改变该值，除非也改变 bootsect 等相应文件
#define SETUP_SECTS 4

// 用于出错时显示语句中表示扇区数
#define STRINGIFY(x) #x

// 显示出错信息，并终止程序
void die(char *str)
{
	fprintf(stderr, "%s\n", str);
	exit(1);
}

// 显示程序使用方法，并退出
void usage(void)
{
	die("Usage: build bootsect setup system [rootdev] [> image]");
}

int main(int argc, char **argv)
{
	int i, c, id;
	char buf[1024];
	char major_root, minor_root;
	struct stat sb;

	// 如果程序命令行参数不是 4 或 5 个，则显示程序用法并退出
	if ((argc != 4) && (argc != 5))
		usage();

	// 如果参数是 5 个，则说明带有根设备名
	if (argc == 5)
	{
		// 如果根设备名是软盘("FLOPPY")，则取该设备文件的状态信息，若出错则显示信息，退出
		if (strcmp(argv[4], "FLOPPY"))
		{
			if (stat(argv[4], &sb))
			{
				perror(argv[4]);
				die("Couldn't stat root device.");
			}
			// 若成功则取该设备名状态结构中的主设备号和次设备号
			major_root = MAJOR(sb.st_rdev);
			minor_root = MINOR(sb.st_rdev);
		}
		else
		{
			// 否则让主设备号和次设备号取 0
			major_root = 0;
			minor_root = 0;
		}
	}
	else
	{
		// 若参数只有 4 个，则让主设备号和次设备号等于系统默认的根设备
		major_root = DEFAULT_MAJOR_ROOT;
		minor_root = DEFAULT_MINOR_ROOT;
	}
	// 在标准错误终端上显示所选择的根设备主、次设备号
	fprintf(stderr, "Root device is (%d, %d)\n", major_root, minor_root);

	// 如果主设备号不等于 2(软盘) 或 3(硬盘)，也不等于 0(取系统默认根设备)，则显示出错信息，退出
	if ((major_root != 2) && (major_root != 3) &&
		(major_root != 0))
	{
		fprintf(stderr, "Illegal root device (major = %d)\n",
				major_root);
		die("Bad root device --- major #");
	}

	// 初始化 buf 缓冲区，全置0
	for (i = 0; i < sizeof buf; i++)
		buf[i] = 0;

	// 以只读方式打开参数 1 指定的文件(bootsect)，若出错则显示出错信息，退出
	if ((id = open(argv[1], O_RDONLY, 0)) < 0)
		die("Unable to open 'boot'");

	// 读取文件中的 minix 执行头部信息(参见列表后说明)，若出错则显示出错信息，退出
	if (read(id, buf, MINIX_HEADER) != MINIX_HEADER)
		die("Unable to read header of 'boot'");

	// 0x0301 - minix 头部 a_magic 魔数；
	// 0x10 - a_flag 可执行；
	// 0x04 - a_cpu, Intel 8086 机器码
	if (((long *)buf)[0] != 0x04100301)
		die("Non-Minix header of 'boot'");

	// 判断头部长度字段 a_hdrlen（字节）是否正确。(后三字节正好没有用，是0)
	if (((long *)buf)[1] != MINIX_HEADER)
		die("Non-Minix header of 'boot'");

	// 判断数据段长 a_data 字段(long)内容是否为 0
	if (((long *)buf)[3] != 0)
		die("Illegal data segment in 'boot'");

	// 判断堆 a_bss 字段(long)内容是否为 0
	if (((long *)buf)[4] != 0)
		die("Illegal bss in 'boot'");

	// 判断执行点 a_entry 字段(long) 内容是否为 0
	if (((long *)buf)[5] != 0)
		die("Non-Minix header of 'boot'");

	// 判断符号表长字段 a_sym 的内容是否为 0
	if (((long *)buf)[7] != 0)
		die("Illegal symbol table in 'boot'");

	// 读取实际代码数据，应该返回读取字节数为 512 字节
	i = read(id, buf, sizeof buf);
	fprintf(stderr, "Boot sector %d bytes.\n", i);
	if (i != 512)
		die("Boot block must be exactly 512 bytes");

	// 判断boot 块 0x510 处是否有可引导标志 0xAA55
	if ((*(unsigned short *)(buf + 510)) != 0xAA55)
		die("Boot block hasn't got boot flag (0xAA55)");

	// 引导块的 508, 509 偏移处存放的是根设备号
	buf[508] = (char)minor_root;
	buf[509] = (char)major_root;

	// 将该 boot 块 512 字节的数据写到标准输出 stdout
	// 若写出字节数不对，则显示出错信息，退出
	i = write(1, buf, 512);
	if (i != 512)
		die("Write call failed");

	// 最后关闭 bootsect 模块文件
	close(id);

	// 现在开始处理 setup 模块
	// 首先以只读方式打开该模块，若出错则显示出错信息，退出
	if ((id = open(argv[2], O_RDONLY, 0)) < 0)
		die("Unable to open 'setup'");

	// 读取该文件中的 minix 执行头部信息(32 字节)，若出错则显示出错信息，退出
	if (read(id, buf, MINIX_HEADER) != MINIX_HEADER)
		die("Unable to read header of 'setup'");

	// 0x0301 - minix 头部a_magic 魔数；
	// 0x10 - a_flag 可执行；
	// 0x04 - a_cpu, Intel 8086 机器码
	if (((long *)buf)[0] != 0x04100301)
		die("Non-Minix header of 'setup'");

	// 判断头部长度字段 a_hdrlen（字节）是否正确。(后三字节正好没有用，是0)
	if (((long *)buf)[1] != MINIX_HEADER)
		die("Non-Minix header of 'setup'");

	// 判断数据段长 a_data 字段(long)内容是否为 0
	if (((long *)buf)[3] != 0)
		die("Illegal data segment in 'setup'");

	// 判断堆 a_bss 字段(long)内容是否为 0
	if (((long *)buf)[4] != 0)
		die("Illegal bss in 'setup'");

	// 判断执行点 a_entry 字段(long) 内容是否为 0
	if (((long *)buf)[5] != 0)
		die("Non-Minix header of 'setup'");

	// 判断符号表长字段 a_sym 的内容是否为 0
	if (((long *)buf)[7] != 0)
		die("Illegal symbol table in 'setup'");

	// 读取随后的执行代码数据，并写到标准输出 stdout
	for (i = 0; (c = read(id, buf, sizeof buf)) > 0; i += c)
		if (write(1, buf, c) != c)
			die("Write call failed");

	// 关闭 setup 模块文件
	close(id);

	// 若 setup 模块长度大于 4 个扇区，则算出错，显示出错信息，退出
	if (i > SETUP_SECTS * 512)
		die("Setup exceeds " STRINGIFY(SETUP_SECTS) " sectors - rewrite build/boot/setup");

	// 在标准错误 stderr 显示 setup 文件的长度值
	fprintf(stderr, "Setup is %d bytes.\n", i);

	// 将缓冲区 buf 清零
	for (c = 0; c < sizeof(buf); c++)
		buf[c] = '\0';

	// 若 setup 长度小于 4*512 字节，则用 \0 将 setup 填足为 4*512 字节
	while (i < SETUP_SECTS * 512)
	{
		c = SETUP_SECTS * 512 - i;
		if (c > sizeof(buf))
			c = sizeof(buf);
		if (write(1, buf, c) != c)
			die("Write call failed");
		i += c;
	}

	// 下面处理 system 模块。首先以只读方式打开该文件
	// 实际为经过处理后的 kernel 模块
	if ((id = open(argv[3], O_RDONLY, 0)) < 0)
		die("Unable to open 'system'");

	// 下面这两个字段经过 objcopy 处理之后，已不适用
	// system 模块是 GCC 格式的文件，先读取 GCC 格式的头部结构信息(linux 的执行文件也采用该格式)
	//	if (read(id,buf,GCC_HEADER) != GCC_HEADER)
	//		die("Unable to read header of 'system'");

	// 该结构中的执行代码入口点字段a_entry 值应为0。
	//	if (((long *) buf)[5] != 0)
	//		die("Non-GCC header of 'system'");

	// 读取随后的执行代码数据，并写到标准输出 stdout
	for (i = 0; (c = read(id, buf, sizeof buf)) > 0; i += c)
		if (write(1, buf, c) != c)
			die("Write call failed");

	// 关闭 system 文件
	close(id);

	// 向 stderr 上打印 system 的字节数
	fprintf(stderr, "System is %d bytes.\n", i);

	// 若system 代码数据长度超过 SYS_SIZE 节 (192 KB)，则显示出错信息，退出
	if (i > SYS_SIZE * 16)
		die("System is too big");

	return (0);
}
