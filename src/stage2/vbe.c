#include "vbe.h"
#include "bios.h"

#define VBE_INFO_ADDR 0x4000
#define VBE_INFO_SEG  0x0400
#define MODE_INFO_ADDR 0x4200
#define MODE_INFO_SEG  0x0420

typedef struct __attribute__((packed)) {
    char     signature[4];      // "VESA" or "VBE2"
    uint16_t version;
    uint16_t oem_string_off;
    uint16_t oem_string_seg;
    uint32_t capabilities;
    uint16_t video_mode_off;
    uint16_t video_mode_seg;
    uint16_t total_memory;      // In 64 KiB blocks
    uint8_t  reserved[492];
} vbe_info_block_t;

typedef struct __attribute__((packed)) {
    uint16_t attributes;
    uint8_t  window_a;
    uint8_t  window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  w_char;
    uint8_t  y_char;
    uint8_t  planes;
    uint8_t  bpp;
    uint8_t  banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  image_pages;
    uint8_t  reserved0;

    uint8_t  red_mask;
    uint8_t  red_pos;
    uint8_t  green_mask;
    uint8_t  green_pos;
    uint8_t  blue_mask;
    uint8_t  blue_pos;
    uint8_t  rsvd_mask;
    uint8_t  rsvd_pos;
    uint8_t  direct_color_attributes;

    uint32_t phys_base_ptr;
    uint32_t offscreen_mem_off;
    uint16_t offscreen_mem_size;
    uint8_t  reserved1[206];
} vbe_mode_info_t;

static uint32_t abs_diff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

// Returns 0 on success, or error code:
// 1 = 0x4F00 (Get Controller Info) failed
// 2 = Mode list pointer was 0
// 3 = No valid LFB Direct-Color mode found in loop
// 4 = 0x4F02 (Set Mode) failed
int vbe_set_mode_debug(uint32_t req_w, uint32_t req_h, uint32_t req_bpp, fb_info_t *out_fb, uint32_t *out_eax) {
    vbe_info_block_t *vbe_info  = (vbe_info_block_t *)VBE_INFO_ADDR;
    vbe_mode_info_t  *mode_info = (vbe_mode_info_t *)MODE_INFO_ADDR;

    for (uint32_t i = 0; i < sizeof(vbe_info_block_t); i++) ((uint8_t *)vbe_info)[i] = 0;

    vbe_info->signature[0] = 'V';
    vbe_info->signature[1] = 'B';
    vbe_info->signature[2] = 'E';
    vbe_info->signature[3] = '2';

    __asm__("xchg %bx, %bx");

    bios_regs_t regs = {0};
    regs.eax = 0x4F00;
    regs.es  = VBE_INFO_SEG;
    regs.edi = 0x0000;
    bios_int(0x10, &regs);

    if (out_eax) *out_eax = regs.eax;
    if ((regs.eax & 0xFFFF) != 0x004F) {
        return 1;
    }

    uint32_t mode_list_linear = ((uint32_t)vbe_info->video_mode_seg << 4) + vbe_info->video_mode_off;
    if (mode_list_linear == 0) {
        return 2;
    }

    const uint16_t *modes = (const uint16_t *)mode_list_linear;
    uint16_t best_mode  = 0xFFFF;
    uint32_t best_score = 0xFFFFFFFF;
    vbe_mode_info_t best_info;

    for (uint32_t i = 0; modes[i] != 0xFFFF; i++) {
        uint16_t mode = modes[i];

        bios_regs_t mregs = {0};
        mregs.eax = 0x4F01;
        mregs.ecx = mode;
        mregs.es  = MODE_INFO_SEG;
        mregs.edi = 0x0000;
        bios_int(0x10, &mregs);

        if ((mregs.eax & 0xFFFF) != 0x004F) continue;
        if ((mode_info->attributes & 0x91) != 0x91) continue;
        if (mode_info->memory_model != 0x06 || mode_info->phys_base_ptr == 0) continue;

        uint32_t score = abs_diff(mode_info->width, req_w)
                       + abs_diff(mode_info->height, req_h)
                       + (abs_diff(mode_info->bpp, req_bpp) * 100);

        if (score < best_score) {
            best_score = score;
            best_mode  = mode;
            best_info  = *mode_info;
            if (score == 0) break;
        }
    }

    if (best_mode == 0xFFFF) {
        return 3;
    }

    bios_regs_t sregs = {0};
    sregs.eax = 0x4F02;
    sregs.ebx = best_mode | 0x4000;
    bios_int(0x10, &sregs);

    if (out_eax) *out_eax = sregs.eax;
    if ((sregs.eax & 0xFFFF) != 0x004F) {
        return 4;
    }

    out_fb->is_graphics = true;
    out_fb->phys_base   = best_info.phys_base_ptr;
    out_fb->pitch       = best_info.pitch;
    out_fb->width       = best_info.width;
    out_fb->height      = best_info.height;
    out_fb->bpp         = best_info.bpp;
    out_fb->red_pos     = best_info.red_pos;
    out_fb->red_mask    = best_info.red_mask;
    out_fb->green_pos   = best_info.green_pos;
    out_fb->green_mask  = best_info.green_mask;
    out_fb->blue_pos    = best_info.blue_pos;
    out_fb->blue_mask   = best_info.blue_mask;

    return 0;
}