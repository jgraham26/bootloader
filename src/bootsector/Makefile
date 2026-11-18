ASM = nasm





bootsector.bin: bootsector.asm
	$(ASM) -f bin -o $@ $<

clean:
	rm -f bootsector.bin