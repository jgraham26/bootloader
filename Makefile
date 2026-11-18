MAX_SECTORS := 64

STAGE1 := src/bootsector/bootsector.bin
STAGE2 := src/stage2/stage2.bin

.PHONY: all bins qemu bochs clean

all: hdd.img

# Target for parent projects (like jackos-v10) that only need the raw binaries
bins: $(STAGE1) $(STAGE2)

hdd.img: $(STAGE1) $(STAGE2)
	dd if=/dev/zero of=$@ bs=512 count=$(MAX_SECTORS) status=none
	dd if=$(STAGE1) of=$@ conv=notrunc status=none
	dd if=$(STAGE2) of=$@ conv=notrunc seek=1 bs=512 status=none
	truncate -s %512 $@
	@SECTORS=$$(( ($$(stat -c "%s" $@) + 511) / 512 )); \
	echo "Size: $$SECTORS sectors (Limit: $(MAX_SECTORS))"; \
	if [ "$$SECTORS" -gt "$(MAX_SECTORS)" ]; then \
		echo "WARNING: $(STAGE2) exceeds $(MAX_SECTORS) sectors (got $$SECTORS)" >&2; \
	fi

$(STAGE1): src/bootsector/bootsector.asm
	$(MAKE) -C src/bootsector

# Delegate directly to src/stage2/Makefile so it tracks its own .c/.asm dependencies
$(STAGE2): FORCE
	$(MAKE) -C src/stage2

FORCE:

qemu: hdd.img
	qemu-system-x86_64 -drive format=raw,file=hdd.img

bochs: hdd.img
	bochs -dbg_gui

clean:
	rm -f hdd.img
	$(MAKE) -C src/bootsector clean
	$(MAKE) -C src/stage2 clean