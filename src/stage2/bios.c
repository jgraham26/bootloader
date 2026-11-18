#include "bios.h"
#include <stdint.h>

#define BOUNCE_ADDR      0x6000
#define BOUNCE_SEG       0x0600
#define BOUNCE_OFF       0x0000
#define MAX_CHUNK_SECTORS 14     // 14 * 512 = 7168 bytes (fits safely in 0x6000..0x7BFF)

typedef struct __attribute__((packed)) {
    uint8_t  size;
    uint8_t  zero;
    uint16_t num_sectors;
    uint16_t buffer_offset;
    uint16_t buffer_segment;
    uint64_t lba;
} dap_t;

static void mem_copy(void* dst, const void* src, uint32_t len) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (len--) {
        *d++ = *s++;
    }
}

bool disk_read_lba(uint8_t drive, uint64_t lba, uint16_t count, void* dest) {
    // place DAP right below boune buffer at 0x5F00
    volatile dap_t* dap = (volatile dap_t*)0x5F00;
    uint8_t* out = (uint8_t*)dest;

    while (count > 0) {
        uint16_t chunk = (count > MAX_CHUNK_SECTORS) ? MAX_CHUNK_SECTORS : count;
        
        dap->size = sizeof(dap_t);
        dap->zero = 0;
        dap->num_sectors = chunk;
        dap->buffer_offset = BOUNCE_OFF;
        dap->buffer_segment = BOUNCE_SEG;
        dap->lba = lba;

        bios_regs_t regs = {0};
        regs.eax = 0x4200;
        regs.edx = drive;
        regs.ds = 0x0000;
        regs.esi = 0x5F00;

        bios_int(0x13, &regs);

        if (regs.eflags & 0x0001) {
            return false;
        }

        mem_copy(out, (const void*)BOUNCE_ADDR, (uint32_t)chunk * 512);

        out += (uint32_t)chunk * 512;
        lba += chunk;
        count -= chunk;
    }

    return true;
}