
# 需要添加调试信息
DEBUG = -g

# GNU 的汇编程序
AS	= as --32 $(DEBUG)

# GNU 的二进制文件处理程序，用于创建、修改以及从归档文件中抽取文件
AR	= ar

# GNU 的连接程序
LD	= ld

# 连接程序所有的参数，默认 32 位程序
LDFLAGS = -m elf_i386

# GNU C 语言编译器
CC	= gcc

# C 预处理选项。-E 只运行 C 预处理
# 对所有指定的 C 程序进行预处理并将处理结果输出到标准输出设备或指定的输出文件中；
CPP	= cpp -nostdinc

# C 编译程序选项
# -Wall 显示所有的警告信息；
# -O 优化选项，优化代码长度和执行时间；
# -fstrength-reduce 优化循环执行代码，排除重复变量；
# -fomit-frame-pointer 省略保存不必要的框架指针；
# -fcombine-regs 合并寄存器，减少寄存器类的使用；
# -finline-functions 将所有简单短小的函数代码嵌入调用程序中；
# -nostdinc 不使用默认路径中的包含文件；

CFLAGS  = $(DEBUG) -m32 \
	-fno-builtin \
	-fno-pic \
	-fno-stack-protector \
	-fomit-frame-pointer \
	-fstrength-reduce \
	-nostdinc 
