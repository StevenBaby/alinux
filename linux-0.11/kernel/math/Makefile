#
# FREAX 内核的 Makefile 文件

# 注意！依赖关系是由'make dep'自动进行的，它也会自动去除原来的依赖信息
# 不要把你自己的依赖关系信息放在这里，除非是特别文件的（也即不是一个.c 文件的信息）
# (Linux 最初的名字叫FREAX，后来被 ftp.funet.fi 的管理员改成 Linux 这个名字)

# 这里导入通用的编译工具以及编译选项
include ../../Option.mk

CFLAGS	+= -I../../include
CPP	+= -I../../include

.c.s:
	$(CC) $(CFLAGS) \
	-S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) \
	-c -o $*.o $<

# 定义目标文件变量 OBJS
OBJS  = math_emulate.o

# 在有了先决条件 OBJS 后使用下面的命令连接成目标 math.a 库文件
math.a: $(OBJS)
	$(AR) rcs math.a $(OBJS)
	sync

# 下面的规则用于清理工作。当执行'make clean'时，就会执行命令，去除所有编译连接生成的文件。
# 'rm'是文件删除命令，选项 -f 含义是忽略不存在的文件，并且不显示删除信息
clean:
	rm -f core *.o *.a tmp_make
	for i in *.c;do rm -f `basename $$i .c`.s;done

# 下面得目标或规则用于检查各文件之间的依赖关系。方法如下：
# 使用字符串编辑程序 sed 对 Makefile 文件（这里即是自己）进行处理
# 输出为删除 Makefile 文件中 '### Dependencies' 行后面的所有行
# 并生成 tmp_make 临时文件；然后对 kernel/ 目录下的每一个 C 文件执行 gcc 预处理操作
# -M 标志告诉预处理程序输出描述每个目标文件相关性的规则，并且这些规则符合 make 语法
# 对于每一个源文件，预处理程序输出一个 make 规则，
# 其结果形式是相应源程序文件的目标文件名加上其依赖关系
# 该源文件中包含的所有头文件列表，把预处理结果都添加到临时文件 tmp_make 中
# 然后将该临时文件复制成新的 Makefile 文件
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile

### Dependencies:
