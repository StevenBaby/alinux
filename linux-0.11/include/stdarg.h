#ifndef _STDARG_H
#define _STDARG_H

// 定义 va_list 是一个字符指针类型
typedef char *va_list;

// 下面给出了类型为 TYPE 的 arg 参数列表所要求的空间容量
// TYPE 也可以是使用该类型的一个表达式

// 下面这句定义了取整后的 TYPE 类型的字节长度值，是 int 长度(4)的倍数
#define __va_rounded_size(TYPE) \
  (((sizeof(TYPE) + sizeof(int) - 1) / sizeof(int)) * sizeof(int))

// 下面这个函数（用宏实现）使 AP 指向传给函数的可变参数表的第一个参数
// 在第一次调用 va_arg 或 va_end 之前，必须首先调用该函数
// __builtin_saveregs() 是在 gcc 的库程序 libgcc2.c 中定义的，用于保存寄存器
#ifndef __sparc__
#define va_start(AP, LASTARG) \
  (AP = ((char *)&(LASTARG) + __va_rounded_size(LASTARG)))
#else
#define va_start(AP, LASTARG) \
  (__builtin_saveregs(),      \
   AP = ((char *)&(LASTARG) + __va_rounded_size(LASTARG)))
#endif

// 下面该宏用于被调用函数完成一次正常返回
// va_end 可以修改 AP 使其在重新调用 va_start 之前不能被使用
// va_end 必须在 va_arg 读完所有的参数后再被调用
void va_end(va_list); /* Defined in gnulib */
#define va_end(AP)

// 下面该宏用于扩展表达式，使其与下一个被传递参数具有相同的类型和值
// 对于缺省值，va_arg 可以用字符、无符号字符和浮点类型
// 在第一次使用 va_arg 时，它返回表中的第一个参数
// 后续的每次调用都将返回表中的下一个参数
// 这是通过先访问 AP，然后把它增加以指向下一项来实现的
// va_arg 使用 TYPE 来完成访问和定位下一项，每调用一次 va_arg
// 它就修改 AP 以指示表中的下一参数
#define va_arg(AP, TYPE)          \
  (AP += __va_rounded_size(TYPE), \
   *((TYPE *)(AP - __va_rounded_size(TYPE))))

#endif /* _STDARG_H */
