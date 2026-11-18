#pragma once

#include <stdint.h>
#include <stdbool.h>


typedef struct {
    uint8_t  drive;
    uint32_t partition_lba;
    uint32_t fat_lba;
    uint32_t cluster_lba;
    uint32_t sectors_per_cluster;
    uint32_t root_cluster;
    uint32_t bytes_per_cluster;
} fat32_fs_t;

// Mounts the first FAT32 partition (type 0x0B or 0x0C) found in the MBR
bool fat32_init(fat32_fs_t *fs, uint8_t drive);

// Finds a file by path (e.g. "/BOOT/KERNEL.BIN") and loads it into `dest_buf`.
// Optionally writes the file size in bytes to `*out_size`.
bool fat32_load_file(const fat32_fs_t *fs, const char *path, void *dest_buf, uint32_t *out_size);