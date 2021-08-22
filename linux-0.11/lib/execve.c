/*
 *  linux/lib/execve.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>

// 加载并执行子进程(其它程序)函数
// 下面该调用宏函数对应：
// int execve(const char * file, char ** argv, char ** envp)
// 参数：
// file - 被执行程序文件名；
// argv - 命令行参数指针数组；
// envp - 环境变量指针数组；
// 直接调用了系统中断 int 0x80
// 参数是 __NR_execve
_syscall3(int, execve, const char *, file, char **, argv, char **, envp)
