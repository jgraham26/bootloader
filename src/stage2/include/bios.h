#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint16_t ds;
    uint16_t es;
    uint16_t eflags;
} __attribute__((packed)) bios_regs_t;

extern void bios_int(uint8_t int_num, bios_regs_t* regs);

bool disk_read_lba(uint8_t drive, uint64_t lba, uint16_t count, void* dest);