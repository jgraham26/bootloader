#include "fat32.h"
#include "bios.h"

#define FAT32_EOC 0x0FFFFFF8

typedef struct {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed)) mbr_partition_t;


typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;    // 0 on FAT32
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat_16;  // 0 on FAT32
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    // FAT32 Extended BPB
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    char     name[11];            // 8.3 uppercase space-padded name
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t first_cluster_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} __attribute__((packed)) fat32_dir_entry_t;


// Scratch buffer for single-sector reads (MBR, VBR, FAT sectors) and 1-cluster dir scans
static uint8_t sector_buf[512];
static uint8_t cluster_buf[32768]; // Supports clusters up to 32 KiB

static uint32_t cluster_to_lba(const fat32_fs_t *fs, uint32_t cluster) {
    return fs->cluster_lba + (cluster - 2) * fs->sectors_per_cluster;
}

static uint32_t fat32_next_cluster(const fat32_fs_t *fs, uint32_t current_cluster) {
    uint32_t fat_offset = current_cluster * 4;
    uint32_t fat_sector = fs->fat_lba + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    if (!disk_read_lba(fs->drive, fat_sector, 1, sector_buf)) {
        return FAT32_EOC;
    }

    uint32_t next = *(uint32_t *)&sector_buf[entry_offset];
    return next & 0x0FFFFFFF; // Top 4 bits are reserved in FAT32
}


static void to_83_name(const char *component, uint32_t len, char out_83[11]) {
    for (int i = 0; i < 11; i++) {
        out_83[i] = ' ';
    }

    uint32_t dst = 0;
    bool in_ext = false;

    for (uint32_t i = 0; i < len; i++) {
        char c = component[i];
        if (c == '.') {
            in_ext = true;
            dst = 8;
            continue;
        }
        if (in_ext && dst >= 11) break;
        if (!in_ext && dst >= 8) continue;

        if (c >= 'a' && c <= 'z') {
            c -= 32; // Uppercase
        }
        out_83[dst++] = c;
    }
}

static bool name_match(const char a[11], const char b[11]) {
    for (int i = 0; i < 11; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

// Searches a directory starting at `dir_cluster` for a 8.3 entry matching `target_83`
static bool find_in_dir(const fat32_fs_t *fs, uint32_t dir_cluster, const char target_83[11], fat32_dir_entry_t *out_entry) {
    uint32_t cluster = dir_cluster;
    uint32_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);

    while (cluster >= 2 && cluster < FAT32_EOC) {
        uint32_t lba = cluster_to_lba(fs, cluster);
        if (!disk_read_lba(fs->drive, lba, (uint16_t)fs->sectors_per_cluster, cluster_buf)) {
            return false;
        }

        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)cluster_buf;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t first_byte = (uint8_t)entries[i].name[0];

            if (first_byte == 0x00) {
                return false; // End of directory
            }
            if (first_byte == 0xE5) {
                continue;     // Deleted entry
            }
            if ((entries[i].attr & 0x0F) == 0x0F) {
                continue;     // Skip Long File Name (LFN) entries
            }
            if (entries[i].attr & 0x08) {
                continue;     // Skip Volume ID label
            }

            if (name_match(entries[i].name, target_83)) {
                *out_entry = entries[i];
                return true;
            }
        }

        cluster = fat32_next_cluster(fs, cluster);
    }

    return false;
}

bool fat32_init(fat32_fs_t *fs, uint8_t drive) {
    fs->drive = drive;

    // 1. Read MBR (LBA 0)
    if (!disk_read_lba(drive, 0, 1, sector_buf)) {
        return false;
    }

    mbr_partition_t *parts = (mbr_partition_t *)&sector_buf[0x1BE];
    uint32_t part_lba = 0;

    for (int i = 0; i < 4; i++) {
        // 0x0B = FAT32 (CHS/LBA), 0x0C = FAT32 (LBA)
        if (parts[i].type == 0x0B || parts[i].type == 0x0C) {
            part_lba = parts[i].lba_start;
            break;
        }
    }

    if (part_lba == 0) {
        return false; // No FAT32 partition found
    }

    // 2. Read FAT32 Volume Boot Record (VBR)
    if (!disk_read_lba(drive, part_lba, 1, sector_buf)) {
        return false;
    }

    fat32_bpb_t *bpb = (fat32_bpb_t *)sector_buf;
    if (bpb->bytes_per_sector != 512 || bpb->sectors_per_fat_32 == 0) {
        return false;
    }

    fs->partition_lba       = part_lba;
    fs->fat_lba             = part_lba + bpb->reserved_sectors;
    fs->cluster_lba         = fs->fat_lba + (bpb->num_fats * bpb->sectors_per_fat_32);
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->root_cluster        = bpb->root_cluster;
    fs->bytes_per_cluster   = bpb->sectors_per_cluster * 512;

    if (fs->bytes_per_cluster > sizeof(cluster_buf)) {
        return false;
    }

    return true;
}

bool fat32_load_file(const fat32_fs_t *fs, const char *path, void *dest_buf, uint32_t *out_size) {
    uint32_t current_cluster = fs->root_cluster;
    fat32_dir_entry_t entry;
    bool found_any = false;

    const char *p = path;
    while (*p == '/') p++; // Skip leading slashes

    while (*p != '\0') {
        // Find end of current path segment
        const char *seg_start = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }
        uint32_t seg_len = (uint32_t)(p - seg_start);

        char target_83[11];
        to_83_name(seg_start, seg_len, target_83);

        if (!find_in_dir(fs, current_cluster, target_83, &entry)) {
            return false;
        }

        current_cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
        found_any = true;

        while (*p == '/') p++;

        // If there are still path components remaining, this entry must be a directory
        if (*p != '\0' && !(entry.attr & 0x10)) {
            return false;
        }
    }

    if (!found_any || (entry.attr & 0x10)) {
        return false; // Path was empty or pointed to a directory, not a file
    }

    if (out_size) {
        *out_size = entry.file_size;
    }

    // Read the file's cluster chain directly into `dest_buf`
    uint8_t *dst = (uint8_t *)dest_buf;
    uint32_t remaining = entry.file_size;
    uint32_t cluster = current_cluster;

    while (remaining > 0 && cluster >= 2 && cluster < FAT32_EOC) {
        uint32_t lba = cluster_to_lba(fs, cluster);

        if (remaining >= fs->bytes_per_cluster) {
            if (!disk_read_lba(fs->drive, lba, (uint16_t)fs->sectors_per_cluster, dst)) {
                return false;
            }
            dst += fs->bytes_per_cluster;
            remaining -= fs->bytes_per_cluster;
        } else {
            // Final partial cluster: read into cluster_buf so we don't overflow `dest_buf`
            if (!disk_read_lba(fs->drive, lba, (uint16_t)fs->sectors_per_cluster, cluster_buf)) {
                return false;
            }
            for (uint32_t i = 0; i < remaining; i++) {
                dst[i] = cluster_buf[i];
            }
            remaining = 0;
        }

        cluster = fat32_next_cluster(fs, cluster);
    }

    return (remaining == 0);
}