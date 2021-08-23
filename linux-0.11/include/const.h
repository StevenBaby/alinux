#ifndef _CONST_H
#define _CONST_H

// 该文件定义了 i 节点中文件属性和类型 i_mode 字段所用到的一些标志位常量符号

// 定义缓冲使用内存的末端(代码中没有使用该常量)
#define BUFFER_END 0x200000

// 指明 i 节点类型
#define I_TYPE 0170000

// 是目录文件。
#define I_DIRECTORY 0040000

// 常规文件，不是目录文件或特殊文件
#define I_REGULAR 0100000

// 块设备特殊文件
#define I_BLOCK_SPECIAL 0060000

// 字符设备特殊文件
#define I_CHAR_SPECIAL 0020000

// 命名管道
#define I_NAMED_PIPE 0010000

// 在执行时设置有效用户 id 类型
#define I_SET_UID_BIT 0004000

// 在执行时设置有效组 id 类型
#define I_SET_GID_BIT 0002000

#endif
