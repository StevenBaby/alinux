#
# 如果你要使用 RAM 盘设备的话，就定义块的大小
# 

RAMDISK =  #-DRAMDISK=512

# 8086 汇编器 
# -0 生成 8086 目标程序；
# -a 生成与 gas 和 gld 部分兼容的代码
AS86	=as86 -0 -a

# 8086 连接器
LD86	=ld86 -0

# 这里导入通用的编译工具以及编译选项
# 与源文件不同之处在于将所有 makefile 文件中的公共部分抽取出来
# 这样如果有变动的话，只需要修改 Option.mk 文件就可以了
include Option.mk

# GNU 连接器 ld 运行时用到的选项
# -Ttext 0 表示内核入口地址为 0
# -e startup_32 表示内核入口为 startup_32
# 意味着 startup_32 这个函数将被加载到 内存开始的位置
LDFLAGS	+= -Ttext 0 -e startup_32

# 添加 include 选项
CFLAGS	+= $(RAMDISK) -Iinclude
CPP	+= -Iinclude

#
# ROOT_DEV 指定在创建内核映像 (image) 文件时所使用的默认根文件系统所在的设备，
# 这可以是软盘(FLOPPY)、/dev/xxxx 或者干脆空着
# 空着时 build 程序（在tools/目录中）就使用默认值 /dev/hd6
# 对应 bochs 中的 ata0-master: type=disk, path="hda.img", mode=flat 
# 

ROOT_DEV= #FLOPPY 

# kernel 目录、mm 目录和 fs 目录所产生的目标代码文件
# 为了方便引用在这里将它们用 ARCHIVES（归档文件）标识符表示
ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o

# 块和字符设备库文件
# .a 表示该文件是个归档文件，通俗的说就是静态链接库，
# 也即包含有许多可执行二进制代码子程序集合的库文件，
# 通常是用 GNU 的 ar 程序生成
# ar 是 GNU 的二进制文件处理程序，用于创建、修改以及从归档文件中抽取文件
DRIVERS =kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a

# 数学运算库文件
MATH	=kernel/math/math.a

# 由 lib/ 目录中的文件所编译生成的通用库文件
LIBS	=lib/lib.a

# make 老式的隐式后缀规则
# 该行指示 make 利用下面的命令将所有的 .c 文件编译生成 .s 汇编程序
# ':' 表示下面是该规则的命令
# 这里的命令相当于 %s: %c
.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<

# 表示将所有 .s 汇编程序文件编译成 .o 目标文件
.s.o:
	$(AS)  -o $*.o $<

# 类似上面，*.c 文件 -> *.o 目标文件
.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<

# all 表示创建 Makefile 所知的最顶层的目标，这里即是 Image 文件
all: Image

# 说明目标（Image 文件）是由冒号后面的 4 个元素产生
# objcopy 用于生成加载到内存中的二进制文件
# 默认 ELF 文件需要解析，特别是对于 bss 段的解析
# objcopy 将 bss 段也存入文件中，尽管全部都是 0
# 这样避免了 ELF 文件的解析，加载到内存中就可以直接执行
# -R 表示删除特定的段
Image: boot/bootsect boot/setup tools/system tools/build
	objcopy -O binary -R .note -R .comment -R .note.gnu.property tools/system tools/kernel
	tools/build boot/bootsect boot/setup tools/kernel $(ROOT_DEV) > Image
	rm tools/kernel -f
	sync # 同步命令是迫使缓冲块数据立即写盘并更新超级块

# 表示disk 这个目标要由 Image 产生，这个目前没用
disk: Image
	dd bs=8192 if=Image of=/dev/fd0

# 由 tools 目录下的 build.c 程序生成执行程序 build
tools/build: tools/build.c
	gcc -m32 -o tools/build tools/build.c

# 生成 head.o 目标文件
boot/head.o: boot/head.s
	gcc $(DEBUG) -m32 -I./include -c $< -o $@

# 生成 system 文件
# nm 命令是 names 的缩写，用于生成符号文件
tools/system:	boot/head.o init/main.o \
	$(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS)
	$(LD) $(LDFLAGS) boot/head.o init/main.o \
	$(ARCHIVES) \
	$(DRIVERS) \
	$(MATH) \
	$(LIBS) \
	-o tools/system 
	nm tools/system | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aU] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)'| sort > System.map 

# 数学协处理函数文件 math.a 由下一行上的命令实现
kernel/math/math.a:
	(cd kernel/math; make)

# 块设备函数文件 blk_drv.a
kernel/blk_drv/blk_drv.a:
	(cd kernel/blk_drv; make)

# 字符设备函数文件 chr_drv.a
kernel/chr_drv/chr_drv.a:
	(cd kernel/chr_drv; make)

# 内核目标模块 kernel.o
kernel/kernel.o:
	(cd kernel; make)

# 内存管理模块 mm.o
mm/mm.o:
	(cd mm; make)

# 文件系统目标模块 fs.o
fs/fs.o:
	(cd fs; make)

# 库函数 lib.a
lib/lib.a:
	(cd lib; make)

# 这里开始的三行是使用 8086 汇编和连接器
boot/setup: boot/setup.s
	$(AS86) -o boot/setup.o boot/setup.s
	$(LD86) -s -o boot/setup boot/setup.o

# 同上，生成 bootsect.o 磁盘引导块
boot/bootsect:	boot/bootsect.s
	$(AS86) -o boot/bootsect.o boot/bootsect.s
	$(LD86) -s -o boot/bootsect boot/bootsect.o

# 这里用来计算 system 的大小，写入 bootsect，但是目前用不到，可以忽略
tmp.s:	boot/bootsect.s tools/system
	(echo -n "SYSSIZE = (";ls -l tools/system | grep system \
		| cut -c25-31 | tr '\012' ' '; echo "+ 15 ) / 16") > tmp.s
	cat boot/bootsect.s >> tmp.s

# 清理生成的文件
clean:
	rm -f Image System.map tmp_make core boot/bootsect boot/setup
	rm -f init/*.o tools/system tools/build boot/*.o
	(cd mm;make clean)
	(cd fs;make clean)
	(cd kernel;make clean)
	(cd lib;make clean)

# 该规则将首先执行上面的 clean 规则，然后对 linux 目录进行压缩
# 生成 backup.Z 压缩文件。'cd .. ' 表示退到linux 的上一级（父）目录；
# 'tar cf - linux' 表示对 linux 目录执行 tar 归档程序
# -cf 表示需要创建新的归档文件 '| compress -' 表示将 tar 程序的执行通过管道操作 ('|')
# 传递给压缩程序 compress，并将压缩程序的输出存成 backup.Z 文件
backup: clean
	(cd .. ; tar cf - linux | compress16 - > backup.Z)
	sync

# 该目标或规则用于各文件之间的依赖关系
# 创建的这些依赖关系是为了给 make 用来确定是否需要要重建一个目标对象的
# 比如当某个头文件被改动过后，make 就通过生成的依赖关系，
# 重新编译与该头文件有关的所有 *.c 文件。具体方法如下：
# 使用字符串编辑程序sed 对 Makefile 文件（这里即是自己）进行处理，
# 输出为删除 Makefile文件中 '### Dependencies' 行后面的所有行，并生成 tmp_make 临时文件
# 然后对 init 目录下的每一个 C 文件（其实只有一个文件 main.c）执行gcc 预处理操作，
# -M 标志告诉预处理程序输出描述每个目标文件相关性的规则
# 并且这些规则符合 make 语法。对于每一个源文件，预处理程序输出一个 make 规则，
# 其结果形式是相应源程序文件的目标文件名加上其依赖关系，该源文件中包含的所有头文件列表
# 下面的 $$i 实际上是 $($i) 的意思。这里 $i 是这句前面的 shell 变量的值
# make 变量如果只有一个字母可以省略圆括号 也就是 $(i) = $i
# 然后把预处理结果都添加到临时文件 tmp_make 中，然后将该临时文件复制成新的 Makefile 文件
dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	(for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done) >> tmp_make
	cp tmp_make Makefile
	(cd fs; make dep)
	(cd kernel; make dep)
	(cd mm; make dep)

### Dependencies:
init/main.o : init/main.c include/unistd.h include/sys/stat.h \
  include/sys/types.h include/sys/times.h include/sys/utsname.h \
  include/utime.h include/time.h include/linux/tty.h include/termios.h \
  include/linux/sched.h include/linux/head.h include/linux/fs.h \
  include/linux/mm.h include/signal.h include/asm/system.h include/asm/io.h \
  include/stddef.h include/stdarg.h include/fcntl.h 
