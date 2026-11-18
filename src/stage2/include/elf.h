#pragma once

#include <stdint.h>
#include <stdbool.h>

bool elf_load(const void *elf_buf, uint32_t elf_size, uint32_t *out_entry);