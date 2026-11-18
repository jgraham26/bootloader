#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MB2_HEADER_MAGIC       0xE85250D6
#define MB2_BOOTLOADER_MAGIC   0x36D76289
#define MB2_SEARCH_LIMIT       32768      // Spec: header must appear within first 32 KiB
#define MB2_ARCH_I386          0

// Multiboot2 Header Tag Types
#define MB2_TAG_END            0
#define MB2_TAG_INFO_REQUEST   1
#define MB2_TAG_ADDRESS        2
#define MB2_TAG_ENTRY_ADDRESS  3
#define MB2_TAG_CONSOLE_FLAGS  4
#define MB2_TAG_FRAMEBUFFER    5
#define MB2_TAG_MODULE_ALIGN   6
#define MB2_TAG_EFI_BS         7
#define MB2_TAG_ENTRY_EFI32    8
#define MB2_TAG_ENTRY_EFI64    9
#define MB2_TAG_RELOCATABLE    10

typedef struct {
    uint32_t magic;
    uint32_t architecture;
    uint32_t header_length;
    uint32_t checksum;
} __attribute__((packed)) mb2_header_t;

typedef struct {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
} __attribute__((packed)) mb2_header_tag_t;

typedef struct {
    uint16_t type;          // 2
    uint16_t flags;
    uint32_t size;          // 24
    uint32_t header_addr;
    uint32_t load_addr;
    uint32_t load_end_addr;
    uint32_t bss_end_addr;
} __attribute__((packed)) mb2_header_tag_address_t;

typedef struct {
    uint16_t type;          // 3
    uint16_t flags;
    uint32_t size;          // 12
    uint32_t entry_addr;
} __attribute__((packed)) mb2_header_tag_entry_t;


typedef struct {
    uint16_t type;          // 5
    uint16_t flags;         // bit 0 = 1 means optional
    uint32_t size;          // 20
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} __attribute__((packed)) mb2_header_tag_framebuffer_t;


typedef struct {
    uint32_t header_offset;       // Byte offset of mb2_header_t inside the kernel file

    // Framebuffer request (Tag 5)
    bool     has_framebuffer;
    bool     framebuffer_optional;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_depth;

    // Raw binary address override (Tag 2 - rare if using ELF, required if non-ELF)
    bool     has_address_tag;
    uint32_t header_addr;
    uint32_t load_addr;
    uint32_t load_end_addr;
    uint32_t bss_end_addr;

    // Entry point override (Tag 3)
    bool     has_entry_tag;
    uint32_t entry_addr;

    // Module alignment (Tag 6)
    bool     align_modules;
} mb2_parsed_header_t;

bool mb2_parse_header(const void* kernel_buf, uint32_t kernel_size, mb2_parsed_header_t* out);