#include <stdint.h>
#include "bios.h"
#include "fat32.h"
#include "mb2.h"
#include "vbe.h"
#include "elf.h"

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_ext;
} __attribute__((packed)) e820_entry_t;

typedef struct {
    uint32_t count;
    e820_entry_t entries[];
} __attribute__((packed)) e820_map_t;

typedef struct __attribute__((packed)) {
    char     signature[8];      // "RSD PTR "
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;          // 0 = ACPI 1.0, >= 2 = ACPI 2.0+
    uint32_t rsdt_addr;
    // ACPI 2.0+ extended fields (valid if revision >= 2):
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} acpi_rsdp_t;

static bool acpi_checksum_valid(const uint8_t *ptr, uint32_t len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum += ptr[i];
    }
    return sum == 0;
}

static const acpi_rsdp_t *scan_rsdp_range(uintptr_t start, uintptr_t end) {
    const char sig[8] = { 'R', 'S', 'D', ' ', 'P', 'T', 'R', ' ' };

    for (uintptr_t addr = start; addr + 20 <= end; addr += 1) {
        const acpi_rsdp_t *cand = (const acpi_rsdp_t *)addr;
        bool match = true;
        for (int i = 0; i < 8; i++) {
            if (cand->signature[i] != sig[i]) {
                match = false;
                break;
            }
        }
        if (!match) continue;

        // Validate ACPI 1.0 20-byte checksum
        if (!acpi_checksum_valid((const uint8_t *)cand, 20)) continue;

        // If ACPI 2.0+ extended length is valid, verify extended checksum too
        if (cand->revision >= 2 && cand->length >= sizeof(acpi_rsdp_t) && cand->length <= 256) {
            if (!acpi_checksum_valid((const uint8_t *)cand, cand->length)) continue;
        }

        return cand;
    }
    return 0;
}

static const acpi_rsdp_t *acpi_find_rsdp(void) {
    // 1. Scan full EBDA up to 0xA0000
    uint16_t ebda_seg = *(volatile uint16_t *)0x040E;
    uintptr_t ebda_phys = (uintptr_t)ebda_seg << 4;
    if (ebda_phys >= 0x80000 && ebda_phys < 0xA0000) {
        const acpi_rsdp_t *rsdp = scan_rsdp_range(ebda_phys, 0xA0000);
        if (rsdp) return rsdp;
    }

    // 2. Scan full BIOS ROM shadow region: 0x000E0000 - 0x000FFFFF
    return scan_rsdp_range(0x000E0000, 0x00100000);
}

static volatile uint16_t* const vga = (volatile uint16_t*)0xB8000;

static void vga_print(const char *str, uint8_t color, uint32_t row) {
    uint32_t col = 0;
    while (str[col] != '\0') {
        vga[row * 80 + col] = (uint16_t)str[col] | ((uint16_t)color << 8);
        col++;
    }
}

static void vga_print_hex(uint32_t val, uint8_t color, uint32_t row, uint32_t col) {
    const char hex[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        vga[row * 80 + col + (7 - i)] = (uint16_t)hex[(val >> (i * 4)) & 0xF] | ((uint16_t)color << 8);
    }
}

static uint8_t *align8(uint8_t *ptr) {
    uint32_t addr = (uint32_t)ptr;
    while (addr & 7u) {
        *(uint8_t *)addr++ = 0;
    }
    return (uint8_t *)addr;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile (    "inb %1, %0"    : "=a"(value)    : "Nd"(port));
    return value;
}


static uint32_t build_mb2_boot_info(uint32_t dest_addr, const e820_map_t *e820, const fb_info_t *fb) {
    uint8_t *base = (uint8_t *)dest_addr;
    uint8_t *p = base + 8;

    // 1. Tag Type 2: Boot Loader Name
    const char loader_name[] = "custom-mb2-loader";
    *(uint32_t *)(p + 0) = 2;
    *(uint32_t *)(p + 4) = 8 + sizeof(loader_name);
    for (uint32_t i = 0; i < sizeof(loader_name); i++) {
        p[8 + i] = (uint8_t)loader_name[i];
    }
    p = align8(p + 8 + sizeof(loader_name));

    // Calculate mem_lower and mem_upper (in KiB) from E820 for Tag Type 4
    uint32_t mem_lower_kb = 639;
    uint32_t mem_upper_kb = 0;
    for (uint16_t i = 0; i < e820->count; i++) {
        if (e820->entries[i].type == 1) {
            uint64_t start = e820->entries[i].base;
            uint64_t end   = start + e820->entries[i].length;
            if (start == 0) {
                mem_lower_kb = (uint32_t)(end / 1024);
            } else if (start <= 0x100000 && end > 0x100000) {
                // Cap at 4 GiB - 1 MiB for 32-bit KiB field
                uint64_t upper = (end - 0x100000) / 1024;
                mem_upper_kb = (upper > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32_t)upper;
            }
        }
    }

    // 2. Tag Type 4: Basic Memory Information
    *(uint32_t *)(p + 0)  = 4;
    *(uint32_t *)(p + 4)  = 16;
    *(uint32_t *)(p + 8)  = mem_lower_kb;
    *(uint32_t *)(p + 12) = mem_upper_kb;
    p = align8(p + 16);

    // 3. Tag Type 6: Memory Map
    uint32_t mmap_size = 16 + (uint32_t)e820->count * 24;
    *(uint32_t *)(p + 0)  = 6;
    *(uint32_t *)(p + 4)  = mmap_size;
    *(uint32_t *)(p + 8)  = 24;
    *(uint32_t *)(p + 12) = 0;
    uint8_t *m_out = p + 16;
    for (uint16_t i = 0; i < e820->count; i++) {
        *(uint64_t *)(m_out + 0)  = e820->entries[i].base;
        *(uint64_t *)(m_out + 8)  = e820->entries[i].length;
        *(uint32_t *)(m_out + 16) = e820->entries[i].type;
        *(uint32_t *)(m_out + 20) = 0;
        m_out += 24;
    }
    p = align8(p + mmap_size);

    // 4. Tag Type 8: Framebuffer Info
    if (fb && fb->is_graphics) {
        *(uint32_t *)(p + 0)  = 8;
        *(uint32_t *)(p + 4)  = 38;
        *(uint64_t *)(p + 8)  = fb->phys_base;
        *(uint32_t *)(p + 16) = fb->pitch;
        *(uint32_t *)(p + 20) = fb->width;
        *(uint32_t *)(p + 24) = fb->height;
        *(uint8_t  *)(p + 28) = fb->bpp;
        *(uint8_t  *)(p + 29) = 1;
        *(uint16_t *)(p + 30) = 0;
        *(uint8_t  *)(p + 32) = fb->red_pos;
        *(uint8_t  *)(p + 33) = fb->red_mask;
        *(uint8_t  *)(p + 34) = fb->green_pos;
        *(uint8_t  *)(p + 35) = fb->green_mask;
        *(uint8_t  *)(p + 36) = fb->blue_pos;
        *(uint8_t  *)(p + 37) = fb->blue_mask;
        p = align8(p + 38);
    }

    const acpi_rsdp_t *rsdp = acpi_find_rsdp();
    if (rsdp) {
        bool is_v2 = (rsdp->revision >= 2 && rsdp->length >= sizeof(acpi_rsdp_t) && rsdp->length <= 256);
        uint32_t tag_type = is_v2 ? 15 : 14;
        uint32_t rsdp_len = is_v2 ? rsdp->length : 20;
        uint32_t tag_size = 8 + rsdp_len;

        *(uint32_t *)(p + 0) = tag_type;
        *(uint32_t *)(p + 4) = tag_size;
        const uint8_t *src = (const uint8_t *)rsdp;
        for (uint32_t i = 0; i < rsdp_len; i++) {
            p[8 + i] = src[i];
        }
        p = align8(p + tag_size);
    }

    // 5. Tag Type 0: End Tag
    *(uint32_t *)(p + 0) = 0;
    *(uint32_t *)(p + 4) = 8;
    p += 8;

    *(uint32_t *)(base + 0) = (uint32_t)(p - base);
    *(uint32_t *)(base + 4) = 0;

    return dest_addr;
}

__attribute__((noreturn))
static void jump_to_kernel(uint32_t entry_addr, uint32_t boot_info_addr) {
    __asm__ volatile (
        "cli\n\t"
        "jmp *%2\n\t"
        :
        : "a"(MB2_BOOTLOADER_MAGIC), "b"(boot_info_addr), "r"(entry_addr)
        : "memory"
    );
    __builtin_unreachable();
}


void stage2_main(uint32_t e820_addr, uint32_t boot_drive) {
    // Clear screen (80x25)
    for (uint32_t i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0720;
    }
    
    
    e820_map_t *map = (e820_map_t *)e820_addr;
    
    vga_print("Stage 2 32-bit C active!", 0x0A, 0);
    vga_print("Boot drive: 0x", 0x0F, 1);
    vga_print_hex(boot_drive, 0x0E, 1, 14);
    
    vga_print("E820 entries: 0x", 0x0F, 2);
    vga_print_hex(map->count, 0x0E, 2, 16);

    uint32_t *raw = (uint32_t *)e820_addr;
    for (int i = 0; i < 8; i++) {
        vga_print_hex(raw[i], 0x0F, 12 + i, 0);
    }

    
    uint8_t sector_buf[512] = {0};
    
    
    fat32_fs_t fs;
    if (!fat32_init(&fs, (uint8_t)boot_drive)) {
        vga_print("FAT32 mount fauled", 0x0C, 4);
        while (1) __asm__ volatile ("cli; hlt");
    }
    
    uint32_t kernel_size = 0;
    void *kernel_staging = (void *)0x800000; // 8 MiB staging buffer
    
    if (!fat32_load_file(&fs, "/BOOT/KERNEL.BIN", kernel_staging, &kernel_size)) {
        vga_print("Failed to load /BOOT/KERNEL.BIN", 0x0C, 5);
        while (1) __asm__ volatile ("cli; hlt");
    }
    
    vga_print("Kernel loaded! Size: 0x", 0x0A, 5);
    vga_print_hex(kernel_size, 0x0E, 5, 23);
    
    
    mb2_parsed_header_t mb2_hdr;
    
    if (!mb2_parse_header(kernel_staging, kernel_size, &mb2_hdr)) {
        vga_print("Invalid or missing Multiboot2 header!", 0x0C, 6);
        while (1) __asm__ volatile ("cli; hlt");
    }
    
    vga_print("MB2 Header found at offset: 0x", 0x0A, 6);
    vga_print_hex(mb2_hdr.header_offset, 0x0E, 6, 30);
    
    if (mb2_hdr.has_framebuffer) {
        vga_print("FB Requested: ", 0x0F, 7);
        vga_print_hex(mb2_hdr.fb_width, 0x0E, 7, 14);
        vga_print_hex(mb2_hdr.fb_height, 0x0E, 7, 23);
        vga_print_hex(mb2_hdr.fb_depth, 0x0E, 7, 32);
        // Next step: call vbe_set_mode(mb2_hdr.fb_width, mb2_hdr.fb_height, mb2_hdr.fb_depth)
    }


    fb_info_t fb = {0};
    if (mb2_hdr.has_framebuffer) {
        uint32_t eax;
        int ret = vbe_set_mode_debug(mb2_hdr.fb_width, mb2_hdr.fb_height, mb2_hdr.fb_depth, &fb, &eax);
        if (ret && !mb2_hdr.framebuffer_optional) {
            vga_print("Required VBE ret, err", 0x0C, 8);
            vga_print_hex(eax, 0xC, 9, 0);
            vga_print_hex(ret, 0xC, 10, 0);
            while (1) __asm__ volatile ("cli; hlt");
        }
    }

    uint32_t entry_point = 0;
    if (!elf_load(kernel_staging, kernel_size, &entry_point)) {
        while (1) __asm__ volatile ("cli; hlt");
    }

    if (mb2_hdr.has_entry_tag) {
        entry_point = mb2_hdr.entry_addr;
    }

    uint32_t boot_info = build_mb2_boot_info(0x20000, map, &fb);

    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    outb(0xA0, 0x20);
    outb(0x20, 0x20);

    jump_to_kernel(entry_point, boot_info);




    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}