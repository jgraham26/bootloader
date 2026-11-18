ASM = nasm
CC = x86_64-jackos-gcc
LD = x86_64-jackos-ld

ASMFLAGS = -f elf32
CFLAGS   = -m32 -ffreestanding -O2 -Wall -Wextra -fno-pie -fno-stack-protector -mgeneral-regs-only -I include
LDFLAGS  = -m elf_i386 -T linker.ld -Map=stage2.map

ASM_SRC = $(shell find -type f -name '*.asm')
C_SRC = $(shell find -type f -name '*.c')

OBJ = $(ASM_SRC:.asm=.asm.o) $(C_SRC:.c=.c.o)


stage2.bin: $(OBJ) linker.ld
	$(LD) $(LDFLAGS) $(OBJ) -o $@

%.c.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.asm.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	rm -f *.o stage2.bin