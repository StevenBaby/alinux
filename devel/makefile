
HARDDISK = ./hda.img
IMAGE = ./floppya.img

.PHONY: ../linux-0.11/Image
../linux-0.11/Image:
	make -C ../linux-0.11

$(IMAGE): ../linux-0.11/Image
	cp $< $@

.PHONY: bochs
bochs: $(IMAGE)
	bochs -q -unlock -f bochsrc

.PHONY: bochsg
bochsg: $(IMAGE)
	bochs-gdb -q -unlock -f bochsrc.gdb

.PHONY: qemu
qemu: $(IMAGE)
	qemu-system-i386 \
		-m 16M \
		-boot a \
		-fda $(IMAGE) \
		-hda $(HARDDISK)

.PHONY: clean
clean:
	rm -rf $(IMAGE)

.PHONY: clean-all
clean-all: clean
	make -C ../linux-0.11 clean
