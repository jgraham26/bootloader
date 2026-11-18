# Custom x86-64 Multiboot2 Bootloader

A lightweight, two-stage legacy BIOS bootloader written in x86 assembly and C, designed to load 32-bit and 64-bit higher-half ELF kernels (`0xFFFFFFFF80000000`) with full Multiboot2 compliance—serving as a minimal, self-contained replacement for GRUB.

## Features

* **Two-Stage Architecture:**
  * **Stage 1 (`bootsector.asm`):** 512-byte MBR boot sector that enables the A20 line, queries the BIOS memory map via `INT 0x15, E820`, loads Stage 2 from disk via BIOS `INT 0x13` LBA extensions, and transitions the CPU into 32-bit Protected Mode.
  * **Stage 2 (`stage2`):** Freestanding 32-bit C bootloader (`0x7E00`) with a Real Mode / Protected Mode BIOS thunking layer for firmware services.
* **FAT32 Filesystem Driver:** Parses MBR partition tables and reads `/BOOT/KERNEL.BIN` directly from a FAT32 boot partition.
* **VBE Graphics Negotiation:** Queries VESA BIOS Extensions (`INT 0x10`) and configures a linear framebuffer (e.g., `1024x768x32` RGB) prior to kernel handoff.
* **ELF32 / ELF64 Loader:** Parses ELF program headers (`PT_LOAD`), copies segments to their target physical load addresses (`p_paddr`), and zero-initializes `.bss` regions.
* **Multiboot2 Boot Information Builder:** Constructs an 8-byte aligned Multiboot2 Boot Information structure (`EAX = 0x36D76289`, `EBX = 0x20000`) containing:
  * **Tag Type 2:** Boot Loader Name
  * **Tag Type 4:** Basic Memory Information (`mem_lower` / `mem_upper`)
  * **Tag Type 6:** E820 Physical Memory Map
  * **Tag Type 8:** VBE Linear Framebuffer Info & RGB bitmasks
  * **Tag Type 14 / 15:** ACPI 1.0 RSDP / ACPI 2.0+ XSDP (scanned from EBDA and `0xE0000`–`0xFFFFF` for IOAPIC / MADT initialization)

---

## Memory Map

| Physical Address Range | Size | Purpose |
| :--- | :--- | :--- |
| `0x00000` – `0x00FFF` | 4 KiB | Real Mode IVT, BDA, and EBDA |
| `0x01000` – `0x03FFF` | 12 KiB | E820 Memory Map Storage (`count` + 24-byte entries) |
| `0x04000` – `0x042FF` | 768 B | VBE Controller & Mode Info Buffers |
| `0x05F00` – `0x07BFF` | ~7.25 KiB | BIOS `INT 0x13` DAP & Sector Bounce Buffer |
| `0x07C00` – `0x07DFF` | 512 B | Stage 1 MBR Boot Sector |
| `0x07E00` – `0x1FFFF` | ~96.5 KiB | Stage 2 Code, Data, and `.bss` |
| `0x20000` – `0x2FFFF` | 64 KiB | Assembled Multiboot2 Boot Information Structure |
| `0x90000` | — | Stage 2 Protected Mode Stack (grows downward) |
| `0x100000` – `0x1FFFFF` | 1 MiB | Final Kernel Physical Load Region (`PT_LOAD` targets) |
| `0x200000`+ | — | Raw Kernel ELF Staging Buffer |

---

## Project Structure

```text
.
├── Makefile
└── src/
    ├── bootsector/
    │   ├── Makefile
    │   └── bootsector.asm      # Stage 1 MBR, A20, E820, Protected Mode switch
    └── stage2/
        ├── Makefile
        ├── stage2.ld           # Stage 2 linker script (base 0x7E00)
        ├── entry.asm           # 32-bit entry stub & 16-bit BIOS thunk
        ├── main.c              # Boot orchestration, ACPI scan, MB2 builder, kernel jump
        ├── bios.c / bios.h     # Disk LBA read helpers via BIOS thunk
        ├── fat32.c / fat32.h   # MBR & FAT32 file loader
        ├── vbe.c / vbe.h       # VESA framebuffer query & mode switch
        └── elf.c / elf.h       # ELF32 / ELF64 segment loader