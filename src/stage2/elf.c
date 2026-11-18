#include "elf.h"

#define ELF_MAGIC  0x464C457F  // "\x7FELF"
#define ELFCLASS32 1
#define ELFCLASS64 2
#define PT_LOAD    1

typedef struct {
    uint32_t magic;
    uint8_t  elf_class;
    uint8_t  endian;
    uint8_t  version;
    uint8_t  os_abi;
    uint8_t  pad[8];
    uint16_t type;
    uint16_t machine;
    uint32_t elf_version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed)) elf32_phdr_t;


typedef struct {
    uint32_t magic;
    uint8_t  elf_class;
    uint8_t  endian;
    uint8_t  version;
    uint8_t  os_abi;
    uint8_t  pad[8];
    uint16_t type;
    uint16_t machine;
    uint32_t elf_version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed)) elf64_phdr_t;

static void copy_segment(uint32_t paddr, const uint8_t *src, uint32_t filesz, uint32_t memsz) {
    uint8_t *dst = (uint8_t *)paddr;
    for (uint32_t i = 0; i < filesz; i++) {
        dst[i] = src[i];
    }
    for (uint32_t i = filesz; i < memsz; i++) {
        dst[i] = 0;
    }
}

bool elf_load(const void *elf_buf, uint32_t elf_size, uint32_t *out_entry) {
    const uint8_t *buf = (const uint8_t *)elf_buf;
    if (elf_size < sizeof(elf32_ehdr_t)) return false;

    const elf32_ehdr_t *id = (const elf32_ehdr_t *)buf;
    if (id->magic != ELF_MAGIC) return false;

    if (id->elf_class == ELFCLASS32) {
        for (uint16_t i = 0; i < id->phnum; i++) {
            const elf32_phdr_t *ph = (const elf32_phdr_t *)(buf + id->phoff + i * id->phentsize);
            if (ph->type == PT_LOAD && ph->memsz > 0) {
                if (ph->offset + ph->filesz > elf_size) return false;
                copy_segment(ph->paddr, buf + ph->offset, ph->filesz, ph->memsz);
            }
        }
        *out_entry = id->entry;
        return true;
    }

    if (id->elf_class == ELFCLASS64) {
        if (elf_size < sizeof(elf64_ehdr_t)) return false;
        const elf64_ehdr_t *eh64 = (const elf64_ehdr_t *)buf;

        for (uint16_t i = 0; i < eh64->phnum; i++) {
            const elf64_phdr_t *ph = (const elf64_phdr_t *)(buf + (uint32_t)eh64->phoff + i * eh64->phentsize);
            if (ph->type == PT_LOAD && ph->memsz > 0) {
                if (ph->offset + ph->filesz > elf_size) return false;
                copy_segment((uint32_t)ph->paddr, buf + (uint32_t)ph->offset, (uint32_t)ph->filesz, (uint32_t)ph->memsz);
            }
        }
        *out_entry = (uint32_t)eh64->entry;
        return true;
    }

    return false;
}