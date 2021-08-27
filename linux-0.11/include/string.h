#ifndef _STRING_H_
#define _STRING_H_

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

extern char *strerror(int errno);

// 这个字符串头文件以内联函数的形式定义了所有字符串操作函数使用 gcc 时
// 同时假定了 ds=es=数据空间，这应该是常规的
// 绝大多数字符串函数都是经手工进行大量优化的
// 尤其是函数 strtok、strstr、str[c]spn，它们应该能正常工作
// 但却不是那么容易理解，所有的操作基本上都是使用寄存器集来完成的，这使得函数即快又整洁
// 所有地方都使用了字符串指令，这又使得代码 “稍微” 难以理解 😊

extern inline char *strcpy(char *dest, const char *src);
extern inline char *strcat(char *dest, const char *src);
extern inline int strcmp(const char *cs, const char *ct);
extern inline int strspn(const char *cs, const char *ct);
extern inline int strcspn(const char *cs, const char *ct);
extern inline char *strpbrk(const char *cs, const char *ct);
extern inline char *strstr(const char *cs, const char *ct);
extern inline int strlen(const char *s);

// 用于临时存放指向下面被分析字符串1(s) 的指针
extern char *___strtok;

extern inline char *strtok(char *s, const char *ct);
extern inline void *memcpy(void *dest, const void *src, int n);
extern inline void *memmove(void *dest, const void *src, int n);
extern inline void *memchr(const void *cs, char c, int count);
#endif
