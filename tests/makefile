
boot.bin: boot.asm
	nasm -f bin $< -o $@

.PHONY: bochs
bochs: boot.bin
	bochs -q -unlock -f bochsrc
