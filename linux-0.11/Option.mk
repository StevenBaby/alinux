DEBUG = -g
AS	= as --32 $(DEBUG)
AR	= ar

LD	= ld

LDFLAGS = -m elf_i386
CC	= gcc
CPP	= cpp -nostdinc
CFLAGS  = $(DEBUG) -m32 \
	-fno-builtin \
	-fno-pic \
	-fno-stack-protector \
	-fomit-frame-pointer \
	-fstrength-reduce \
	-nostdinc 
