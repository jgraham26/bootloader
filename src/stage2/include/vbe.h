#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool     is_graphics;
    uint32_t phys_base;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  red_pos;
    uint8_t  red_mask;
    uint8_t  green_pos;
    uint8_t  green_mask;
    uint8_t  blue_pos;
    uint8_t  blue_mask;
} fb_info_t;

int vbe_set_mode_debug(uint32_t req_w, uint32_t req_h, uint32_t req_bpp, fb_info_t *out_fb, uint32_t *out_eax);