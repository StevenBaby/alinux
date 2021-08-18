#
# FREAX 内核的 Makefile 文件

# 注意！依赖关系是由'make dep'自动进行的，它也会自动去除原来的依赖信息
# 不要把你自己的依赖关系信息放在这里，除非是特别文件的（也即不是一个.c 文件的信息）
# (Linux 最初的名字叫FREAX，后来被 ftp.funet.fi 的管理员改成 Linux 这个名字)

# 这里导入通用的编译工具以及编译选项
include ../Option.mk

LDFLAGS	+= -r

CFLAGS	+= -I../include

CPP	+= -I../include

# 下面的规则指示 make 利用下面的命令将所有的 .c 文件编译生成 .s 汇编程序。
# 该规则的命令使 gcc 采用 CFLAGS 所指定的选项对 C 代码编译后不进行汇编就停止（-S）
# 从而产生与输入的各个 C 文件对应的汇编代码文件
# 默认情况下所产生的汇编程序文件名是原 C 文件名去掉.c 而加上.s 后缀。-o 表示其后是输出文件的名称。
# 其中 $*.s（或$@）是自动目标变量，$<代表第一个先决条件，这里即是符合条件 *.c 的文件

.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<

# 下面规则表示将所有 .s 汇编程序文件编译成 .o 目标文件
.s.o:
	$(AS) -o $*.o $<

# 类似上面，*.c 文件 -> *.o 目标文件，不进行连接
.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<

# 定义目标文件变量 OBJS
OBJS  = sched.o system_call.o traps.o asm.o fork.o \
	panic.o printk.o vsprintf.o sys.o exit.o \
	signal.o mktime.o

# 在有了先决条件 OBJS 后使用下面的命令连接成目标 kernel.o
kernel.o: $(OBJS)
	$(LD) $(LDFLAGS) -o kernel.o $(OBJS)
	sync

# 下面的规则用于清理工作。当执行'make clean'时，就会执行命令，去除所有编译连接生成的文件。
# 'rm'是文件删除命令，选项 -f 含义是忽略不存在的文件，并且不显示删除信息
clean:
	rm -f core *.o *.a tmp_make keyboard.s
	for i in *.c;do rm -f `basename $$i .c`.s;done
	(cd chr_drv; make clean) # 进入 chr_drv/ 目录；执行该目录 Makefile 中的 clean 规则
	(cd blk_drv; make clean)
	(cd math; make clean)

# 下面得目标或规则用于检查各文件之间的依赖关系。方法如下：
# 使用字符串编辑程序 sed 对 Makefile 文件（这里即是自己）进行处理
# 输出为删除 Makefile 文件中 '### Dependencies' 行后面的所有行
# 并生成 tmp_make 临时文件；然后对 kernel/ 目录下的每一个 C 文件执行 gcc 预处理操作
# -M 标志告诉预处理程序输出描述每个目标文件相关性的规则，并且这些规则符合 make 语法
# 对于每一个源文件，预处理程序输出一个 make 规则，
# 其结果形式是相应源程序文件的目标文件名加上其依赖关系
# 该源文件中包含的所有头文件列表，把预处理结果都添加到临时文件 tmp_make 中
# 然后将该临时文件复制成新的 Makefile 文件。
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in *.c;do echo -n `echo $$i | sed 's,\.c,\.s,'`" "; \
		$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile
	(cd chr_drv; make dep) # 对 chr_drv/ 目录下的 Makefile 文件也作同样的处理
	(cd blk_drv; make dep)

### Dependencies:
exit.s exit.o: exit.c ../include/errno.h ../include/signal.h \
  ../include/sys/types.h ../include/sys/wait.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/linux/mm.h \
  ../include/linux/kernel.h ../include/linux/tty.h ../include/termios.h \
  ../include/asm/segment.h
fork.s fork.o: fork.c ../include/errno.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/segment.h ../include/asm/system.h
mktime.s mktime.o: mktime.c ../include/time.h
panic.s panic.o: panic.c ../include/linux/kernel.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h
printk.s printk.o: printk.c ../include/stdarg.h ../include/stddef.h \
  ../include/linux/kernel.h
sched.s sched.o: sched.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/linux/kernel.h ../include/linux/sys.h \
  ../include/linux/fdreg.h ../include/asm/system.h ../include/asm/io.h \
  ../include/asm/segment.h
signal.s signal.o: signal.c ../include/linux/sched.h ../include/linux/head.h \
  ../include/linux/fs.h ../include/sys/types.h ../include/linux/mm.h \
  ../include/signal.h ../include/linux/kernel.h ../include/asm/segment.h
sys.s sys.o: sys.c ../include/errno.h ../include/linux/sched.h \
  ../include/linux/head.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/tty.h \
  ../include/termios.h ../include/linux/kernel.h ../include/asm/segment.h \
  ../include/sys/times.h ../include/sys/utsname.h
traps.s traps.o: traps.c ../include/string.h ../include/linux/head.h \
  ../include/linux/sched.h ../include/linux/fs.h ../include/sys/types.h \
  ../include/linux/mm.h ../include/signal.h ../include/linux/kernel.h \
  ../include/asm/system.h ../include/asm/segment.h ../include/asm/io.h
vsprintf.s vsprintf.o: vsprintf.c ../include/stdarg.h ../include/string.h
