#ifndef _A_OUT_H
#define _A_OUT_H

#define __GNU_EXEC_MACROS__

// 执行文件头结构
struct exec
{
	unsigned long a_magic; // 执行文件魔数。使用 N_MAGIC 等宏访问
	unsigned a_text;	   // 代码长度，字节数
	unsigned a_data;	   // 数据长度，字节数
	unsigned a_bss;		   // 文件中的未初始化数据区长度，字节数
	unsigned a_syms;	   // 文件中的符号表长度，字节数
	unsigned a_entry;	   // 执行开始地址
	unsigned a_trsize;	   // 代码重定位信息长度，字节数
	unsigned a_drsize;	   // 数据重定位信息长度，字节数
};

// 用于取执行结构中的魔数
#ifndef N_MAGIC
#define N_MAGIC(exec) ((exec).a_magic)
#endif

#ifndef OMAGIC

// 指明为目标文件或者不纯的可执行文件的代号
#define OMAGIC 0407
/* Code indicating pure executable.  */

// 历史上，最早在 PDP-11 计算机上，魔数（幻数）是八进制数 0407，它位于执行程序头结构的开始处
// 原本是 PDP-11 的一条跳转指令，表示跳转到随后 7 个字后的代码开始处
// 这样，加载程序(loader) 就可以在把执行文件放入内存后，直接跳转到指令开始处运行
// 现在已经没有程序使用这种方法了，但这个八进制数却作为识别文件类型的标志（魔数）保留了下来
// OMAGIC 可以认为是 Old Magic 的意思

// 指明为纯可执行文件的代号
// New Magic, 1975  年以后开始使用，涉及虚存机制
#define NMAGIC 0410

// 指明为需要分页处理的可执行文件
#define ZMAGIC 0413
#endif /* not OMAGIC */

// 另外还有一个 QMAGIC，是为了节约磁盘容量，把盘上执行文件的头结构与代码紧凑存放
// 如果魔数不能被识别，则返回真

#ifndef N_BADMAG
#define N_BADMAG(x) \
	(N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC && N_MAGIC(x) != ZMAGIC)
#endif

#define _N_BADMAG(x) \
	(N_MAGIC(x) != OMAGIC && N_MAGIC(x) != NMAGIC && N_MAGIC(x) != ZMAGIC)

// 程序头在内存中的偏移位置
#define _N_HDROFF(x) (SEGMENT_SIZE - sizeof(struct exec))

// 代码起始偏移值
#ifndef N_TXTOFF
#define N_TXTOFF(x) \
	(N_MAGIC(x) == ZMAGIC ? _N_HDROFF((x)) + sizeof(struct exec) : sizeof(struct exec))
#endif

// 数据起始偏移值
#ifndef N_DATOFF
#define N_DATOFF(x) (N_TXTOFF(x) + (x).a_text)
#endif

// 代码重定位信息偏移值
#ifndef N_TRELOFF
#define N_TRELOFF(x) (N_DATOFF(x) + (x).a_data)
#endif

// 数据重定位信息偏移值
#ifndef N_DRELOFF
#define N_DRELOFF(x) (N_TRELOFF(x) + (x).a_trsize)
#endif

// 符号表偏移值
#ifndef N_SYMOFF
#define N_SYMOFF(x) (N_DRELOFF(x) + (x).a_drsize)
#endif

// 字符串信息偏移值
#ifndef N_STROFF
#define N_STROFF(x) (N_SYMOFF(x) + (x).a_syms)
#endif

// 代码段加载到内存中后的地址
#ifndef N_TXTADDR
#define N_TXTADDR(x) 0
#endif

// 数据段加载到内存中后的地址
// 注意，对于下面没有列出名称的机器，需要你自己来定义对应的 SEGMENT_SIZE
#if defined(vax) || defined(hp300) || defined(pyr)
#define SEGMENT_SIZE PAGE_SIZE
#endif
#ifdef hp300
#define PAGE_SIZE 4096
#endif
#ifdef sony
#define SEGMENT_SIZE 0x2000
#endif /* Sony.  */
#ifdef is68k
#define SEGMENT_SIZE 0x20000
#endif
#if defined(m68k) && defined(PORTAR)
#define PAGE_SIZE 0x400
#define SEGMENT_SIZE PAGE_SIZE
#endif

#define PAGE_SIZE 4096
#define SEGMENT_SIZE 1024

// 以段为界的大小
#define _N_SEGMENT_ROUND(x) (((x) + SEGMENT_SIZE - 1) & ~(SEGMENT_SIZE - 1))

// 代码段尾地址
#define _N_TXTENDADDR(x) (N_TXTADDR(x) + (x).a_text)

// 数据开始地址
#ifndef N_DATADDR
#define N_DATADDR(x)                           \
	(N_MAGIC(x) == OMAGIC ? (_N_TXTENDADDR(x)) \
						  : (_N_SEGMENT_ROUND(_N_TXTENDADDR(x))))
#endif

// bss 段加载到内存以后的地址
#ifndef N_BSSADDR
#define N_BSSADDR(x) (N_DATADDR(x) + (x).a_data)
#endif

// nlist 结构。符号表记录结构
#ifndef N_NLIST_DECLARED
struct nlist
{
	union
	{
		char *n_name;
		struct nlist *n_next;
		long n_strx;
	} n_un;
	unsigned char n_type;
	char n_other;
	short n_desc;
	unsigned long n_value;
};
#endif

// 下面定义 exec 结构中的变量偏移值
#ifndef N_UNDF
#define N_UNDF 0
#endif
#ifndef N_ABS
#define N_ABS 2
#endif
#ifndef N_TEXT
#define N_TEXT 4
#endif
#ifndef N_DATA
#define N_DATA 6
#endif
#ifndef N_BSS
#define N_BSS 8
#endif
#ifndef N_COMM
#define N_COMM 18
#endif
#ifndef N_FN
#define N_FN 15
#endif

// 以下 3 个常量定义是 nlist 结构中 n_type 变量的屏蔽码
#ifndef N_EXT
#define N_EXT 1
#endif
#ifndef N_TYPE
#define N_TYPE 036
#endif
#ifndef N_STAB
#define N_STAB 0340
#endif

// 下面的类型指明了符号的定义作为对另一个符号的间接引用
// 紧接该符号的其它的符号呈现为未定义的引用间接性是不对称的
// 其它符号的值将被用于满足间接符号的请求，但反之不然
// 如果其它符号并没有定义，则将搜索库来寻找一个定义
#define N_INDR 0xa

// 下面的符号与集合元素有关，所有具有相同名称 N_SET[ATDB] 的符号形成一个集合
// 在代码部分中已为集合分配了空间，并且每个集合元素的值存放在一个字（word）的空间
// 空间的第一个字存有集合的长度（集合元素数目），集合的地址被放入一个 N_SETV 符号，它的名称与集合同名
// 在满足未定义的外部引用方面，该符号的行为象一个 N_DATA 全局符号

// 以下这些符号在目标文件中是作为链接程序 LD 的输入

// 绝对集合元素符号
#define N_SETA 0x14

// 代码集合元素符号
#define N_SETT 0x16

// 数据集合元素符号
#define N_SETD 0x18

// Bss 集合元素符号
#define N_SETB 0x1A

// 下面是 LD 的输出

// 指向数据区中集合向量
#define N_SETV 0x1C

#ifndef N_RELOCATION_INFO_DECLARED

// 下面的结构描述执行一个重定位的操作
// 文件的代码重定位部分是这些结构的一个向量，所有这些适用于代码部分
// 类似地，数据重定位部分适用于数据部分

// 重定位信息结构
struct relocation_info
{
	// 需要重定位的地址（在段内）
	int r_address;

	// r_symbolnum 的含义与r_extern 有关
	unsigned int r_symbolnum : 24;

	//  非零意味着值是一个 pc 相关的偏移值
	// 因而在其自己地址空间以及符号或指定的节改变时，需要被重定位
	unsigned int r_pcrel : 1;

	// 需要被重定位的字段长度（是2 的次方），因此，若值是 2 则表示 1<<2 字节数
	unsigned int r_length : 2;

	// 1 => 以符号的值重定位
	// r_symbolnum 是文件符号表中符号的索引。
	// 0 => 以段的地址进行重定位
	// r_symbolnum 是N_TEXT、N_DATA、N_BSS 或 N_ABS
	// (N_EXT 比特位也可以被设置，但是毫无意义)
	unsigned int r_extern : 1;

	//  没有使用的 4 个比特位，但是当进行写一个目标文件时最好将它们清除掉
	unsigned int r_pad : 4;
};
#endif /* no N_RELOCATION_INFO_DECLARED.  */

#endif /* __A_OUT_GNU_H__ */
