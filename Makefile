MAX_SECTORS = 64

.PHONY: src/stage2/stage2.bin clean

hdd.img: src/bootsector/bootsector.bin src/stage2/stage2.bin
	dd if=/dev/zero of=hdd.img bs=512 count=$(MAX_SECTORS)
	dd if=src/bootsector/bootsector.bin of=hdd.img conv=notrunc
	dd if=src/stage2/stage2.bin of=hdd.img conv=notrunc seek=1 bs=512
	truncate -s %512 hdd.img
	@SECTORS=$$(( ($$(stat -c "%s" $@) + 511) / 512 )); \
	echo "Size: $$SECTORS sectors (Limit: $(MAX_SECTORS))"; \
	if [ "$$SECTORS" -gt "$(MAX_SECTORS)" ]; then \
		echo "WARNING: $< exceeds $(MAX_SECTORS) sectors (got $$SECTORS)" >&2; \
	fi


src/bootsector/bootsector.bin: src/bootsector/bootsector.asm
	pushd src/bootsector && $(MAKE) && popd

src/stage2/stage2.bin:
	pushd src/stage2 && $(MAKE) && popd


qemu: hdd.img
	qemu-system-x86_64 -drive format=raw,file=hdd.img

bochs: hdd.img
	bochs -dbg_gui

clean:
	rm -f hdd.img
	pushd src/bootsector && $(MAKE) clean && popd
	pushd src/stage2 && $(MAKE) clean && popd