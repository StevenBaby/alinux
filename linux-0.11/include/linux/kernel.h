// 定义了一些常用函数的原型等

// 验证给定地址开始的内存块是否超限，若超限则追加内存
void verify_area(void *addr, int count);

// 显示内核出错信息，然后进入死循环
volatile void panic(const char *str);

// 标准打印（显示）函数
int printf(const char *fmt, ...);

// 内核专用的打印信息函数，功能与 printf() 相同
int printk(const char *fmt, ...);

// 往 tty 上写指定长度的字符串
int tty_write(unsigned ch, char *buf, int count);

// 通用内核内存分配函数
void *malloc(unsigned int size);

// 释放指定对象占用的内存
void free_s(void *obj, int size);

#define free(x) free_s((x), 0)

// 下面函数是以宏的形式定义的，但是在某方面来看它可以成为一个真正的子程序
// 如果返回是 true 时它将设置标志
// （如果使用 root 用户权限的进程设置了标志，则用于执行 BSD 方式的计帐处理）
// 这意味着你应该首先执行常规权限检查，最后再检测 suser()

#define suser() (current->euid == 0)
