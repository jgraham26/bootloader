#include <system.h>
#include <io.h>

#define EI_NIDENT 16
#define PT_LOAD   1

extern uint32_t partition_lba;
uint32_t mmap_count;

// ---------------- ELF HEADER STRUCTS ----------------

typedef struct {
  unsigned char e_ident[EI_NIDENT]; 
  uint16_t     e_type;         
  uint16_t     e_machine;      
  uint32_t     e_version;      
  uint64_t     e_entry;        
  uint64_t      e_phoff;        
  uint64_t      e_shoff;        
  uint32_t     e_flags;        
  uint16_t     e_ehsize;       
  uint16_t     e_phentsize;    
  uint16_t     e_phnum;        
  uint16_t     e_shentsize;    
  uint16_t     e_shnum;        
  uint16_t     e_shstrndx;     
} __attribute__((packed)) Elf64_Ehdr;

typedef struct
{
  uint32_t    p_type;         
  uint32_t    p_flags;        
  uint64_t     p_offset;       
  uint64_t    p_vaddr;        
  uint64_t    p_paddr;        
  uint64_t   p_filesz;       
  uint64_t   p_memsz;        
  uint64_t   p_align;        
} __attribute__((packed)) Elf64_Phdr;

static inline uint32_t to_phys(uint32_t virt) {
    if (virt >= 0x80000000) {
        return (uint32_t)(virt - 0x80000000);
    }
    return (uint32_t)virt;
}

// ---------------- ELF LOADER ----------------


uint32_t load_elf64_from_buffer(const uint8_t *elf, uint32_t elf_size) {
    uint32_t begin = 0xFFFFFFFF;
    if (elf_size < sizeof(Elf64_Ehdr)) {
        return -1; // Too small to be ELF
    }

    const Elf64_Ehdr *hdr = (const Elf64_Ehdr *)elf;

    // Check ELF magic
    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L'  || hdr->e_ident[3] != 'F') {
        return -2; // Invalid ELF
    }

    // Only support ELF64 executable
    if (hdr->e_type != 2) { // ET_EXEC
        return -3;
    }

    clearScreen();

    // Calculate exactly where the program headers start in bytes
    const uint8_t *ph_start = elf + (uint32_t)hdr->e_phoff;

    for (int i = 0; i < hdr->e_phnum; i++) {
        // Get a pointer directly to the current program header bytes
        const uint8_t *curr_ph_bytes = ph_start + (i * hdr->e_phentsize);

        // Read p_type (Offset 0, 4 bytes)
        uint32_t p_type = *(const uint32_t *)(curr_ph_bytes + 0);

        if (p_type != PT_LOAD)
            continue;

        // Read 64-bit p_offset (Offset 8, 8 bytes) and grab lower 32 bits
        uint32_t p_offset_low = *(const uint32_t *)(curr_ph_bytes + 8);

        // Read 64-bit p_vaddr (Offset 16, 8 bytes) and grab lower 32 bits
        uint32_t p_vaddr_low = *(const uint32_t *)(curr_ph_bytes + 16);

        // Read 64-bit p_filesz (Offset 32, 8 bytes) and grab lower 32 bits
        uint32_t p_filesz_low = *(const uint32_t *)(curr_ph_bytes + 32);

        // Read 64-bit p_memsz (Offset 40, 8 bytes) and grab lower 32 bits
        uint32_t p_memsz_low = *(const uint32_t *)(curr_ph_bytes + 40);

        // Perform your translation safely on the low 32 bits
        // Since 0xFFFFFFFF80100000 low 32 bits is exactly 0x00100000, we don't even need subtraction!
        uint8_t *dest = (uint8_t *)to_phys(p_vaddr_low); 
        const uint8_t *src  = (const uint8_t *)((uint32_t)elf + p_offset_low);

        if ((uint32_t)dest < begin) {
            begin = (uint32_t)dest;
        }

        // Copy from file to proper memory addr
        memcpy(dest, src, p_filesz_low);

        printf("Segment %d: Loaded %d bytes from 0x%x to 0x%x\n", i, p_filesz_low, src, dest);

        // Zero out extra memory if memsz > filesz
        if (p_memsz_low > p_filesz_low) {
            memset(dest + p_filesz_low, 0, p_memsz_low - p_filesz_low);
        }
    }
    uint32_t phys_entry = to_phys(hdr->e_entry);
    printf("Physical Entry point: 0x%x\n", phys_entry);

    boot_info->magic = MAGIC;

    // video
    memcpy(&boot_info->video, video_info, sizeof(struct videoInfo));
    // elf
    boot_info->elf.kernel_load_addr = begin;
    boot_info->elf.kernel_size = elf_size;
    boot_info->elf.entry = hdr->e_entry;
    // drive
    boot_info->boot_drive.drive = fat.drive;
    boot_info->boot_drive.kernel_partition_lba = partition_lba;
    switch (fat.fat_type) {
        case 12:
            boot_info->boot_drive.fs = FS_FAT12;
            break;
        case 16:
            boot_info->boot_drive.fs = FS_FAT16;
            break;
        case 32:
            boot_info->boot_drive.fs = FS_FAT32;
            break;
    }
    // mmap
    boot_info->mmap.mmap = (e820_entry_t*)0x2000;
    boot_info->mmap.mmap_count = mmap_count;

    // Jump to entry point
    entry_func_t entry = (entry_func_t)((uint32_t)phys_entry);



    kernel_jmp(entry);

    return 0;
}