#ifndef _CONFIG_H
#define _CONFIG_H

// 内核配置头文件，定义使用的键盘语言类型和硬盘类型（HD_TYPE）可选项

// 根文件系统设备已不再是硬编码的了
// 通过修改 boot/bootsect.s 文件中行 ROOT_DEV = XXX
// 你可以改变根设备的默认设置值

// 在这里定义你的键盘类型
// KBD_FINNISH 是芬兰键盘
// KBD_US 是美式键盘
// KBD_GR 是德式键盘
// KBD_FR 是法式键盘

#define KBD_US
/*#define KBD_GR */
/*#define KBD_FR */
/*#define KBD_FINNISH */

// 通常，Linux 能够在启动时从 BIOS 中获取驱动器的参数
// 但是若由于未知原因而没有得到这些参数时，会使程序束手无策
// 对于这种情况，你可以定义 HD_TYPE，其中包括硬盘的所有信息

// HD_TYPE 宏应该象下面这样的形式：

// #define HD_TYPE { head, sect, cyl, wpcom, lzone, ctl}

// 对于有两个硬盘的情况，参数信息需用逗号分开：

// #define HD_TYPE { h,s,c,wpcom,lz,ctl }, {h,s,c,wpcom,lz,ctl }

// 下面是一个例子，两个硬盘，第 1 个是类型 2，第 2 个是类型 3：

// #define HD_TYPE { 4,17,615,300,615,8 }, {6,17,615,300,615,0 }

// 注意：对应所有硬盘
// 若其磁头数<=8，则 ctl 等于 0，
// 若磁头数多于 8 个，则 ctl=8

// 如果你想让 BIOS 给出硬盘的类型，那么只需不定义 HD_TYPE，这是默认操作

#endif
