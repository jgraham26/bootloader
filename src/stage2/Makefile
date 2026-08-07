CC = i686-elf-gcc
CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -fno-stack-protector -Wall -Wextra -pedantic -Werror -I include -Oz -Wno-unused
	
LDFLAGS = -m32 -ffreestanding -nostdlib -fno-stack-protector -Wl,--build-id=none -Wl,--gc-sections -T linker.ld

C_SRC = $(wildcard *.c)
ASM_SRC = $(wildcard *.S)
OBJ = $(C_SRC:.c=.c.o) $(ASM_SRC:.S=.S.o)

stage2.bin: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)
	truncate -s 31K stage2.bin

%.c.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

%.S.o: %.S
	$(CC) $(CFLAGS) -c -o $@ $^

clean:
	find -name '*.o' -delete
	find -name '*.bin' -delete
