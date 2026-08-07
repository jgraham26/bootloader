.PHONY: all clean qemu bochs kernel.elf bootsect stage2
default: disk.img

disk.img: bootsect stage2 kernel.elf
	bash makeDisk.sh
kernel.elf:
	$(MAKE) -C src/test_kernel
	cp src/test_kernel/kernel.elf kernel.elf

bootsect:
	$(MAKE) -C src/bootsect

qemu: disk.img
	qemu-system-x86_64 -hda disk.img -enable-kvm

bochs: disk.img
	bochs -f bochsrc.txt -dbg_gui

stage2:
	$(MAKE) -C src/stage2

clean:
	$(MAKE) -C src/stage2 clean
	$(MAKE) -C src/bootsect clean
	$(MAKE) -C src/test_kernel clean
	rm -f disk.img kernel.elf