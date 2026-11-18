#include "mb2.h"

static inline uint32_t align_up_8(uint32_t val) {
    return (val + 7u) & ~7u;
}

bool mb2_parse_header(const void *kernel_buf, uint32_t kernel_size, mb2_parsed_header_t *out) {
    // Zero out the output struct
    uint8_t *p = (uint8_t *)out;
    for (uint32_t i = 0; i < sizeof(mb2_parsed_header_t); i++) {
        p[i] = 0;
    }

    const uint8_t *buf = (const uint8_t *)kernel_buf;
    uint32_t search_max = (kernel_size < MB2_SEARCH_LIMIT) ? kernel_size : MB2_SEARCH_LIMIT;

    if (search_max < sizeof(mb2_header_t)) {
        return false;
    }

    // 1. Scan on 8-byte aligned boundaries for the Multiboot2 magic number
    const mb2_header_t *hdr = 0;
    uint32_t hdr_offset = 0;

    for (uint32_t off = 0; off <= search_max - sizeof(mb2_header_t); off += 8) {
        const mb2_header_t *candidate = (const mb2_header_t *)(buf + off);

        if (candidate->magic == MB2_HEADER_MAGIC) {
            // Validate checksum: magic + architecture + header_length + checksum == 0
            uint32_t sum = candidate->magic
                         + candidate->architecture
                         + candidate->header_length
                         + candidate->checksum;

            if (sum == 0 && candidate->architecture == MB2_ARCH_I386) {
                // Ensure the full header fits inside the loaded kernel file
                if (off + candidate->header_length <= kernel_size &&
                    candidate->header_length >= sizeof(mb2_header_t)) {
                    hdr = candidate;
                    hdr_offset = off;
                    break;
                }
            }
        }
    }

    if (!hdr) {
        return false; // No valid Multiboot2 header found
    }

    out->header_offset = hdr_offset;

    // 2. Walk header tags starting immediately after the 16-byte fixed header
    uint32_t tag_offset = sizeof(mb2_header_t);

    while (tag_offset + sizeof(mb2_header_tag_t) <= hdr->header_length) {
        const mb2_header_tag_t *tag = (const mb2_header_tag_t *)((const uint8_t *)hdr + tag_offset);

        // Sanity check tag size
        if (tag->size < sizeof(mb2_header_tag_t) || tag_offset + tag->size > hdr->header_length) {
            return false;
        }

        // Tag 0 marks the end of the header tag list
        if (tag->type == MB2_TAG_END) {
            break;
        }

        bool is_optional = (tag->flags & 1) != 0;

        switch (tag->type) {
            case MB2_TAG_INFO_REQUEST:
                // Contains an array of uint32_t mbi_tag_types requested by the kernel.
                // You can inspect them here if you want to fail early on unsupported tags.
                break;

            case MB2_TAG_ADDRESS: {
                if (tag->size < sizeof(mb2_header_tag_address_t)) return false;
                const mb2_header_tag_address_t *addr_tag = (const mb2_header_tag_address_t *)tag;
                out->has_address_tag = true;
                out->header_addr     = addr_tag->header_addr;
                out->load_addr       = addr_tag->load_addr;
                out->load_end_addr   = addr_tag->load_end_addr;
                out->bss_end_addr    = addr_tag->bss_end_addr;
                break;
            }

            case MB2_TAG_ENTRY_ADDRESS: {
                if (tag->size < sizeof(mb2_header_tag_entry_t)) return false;
                const mb2_header_tag_entry_t *entry_tag = (const mb2_header_tag_entry_t *)tag;
                out->has_entry_tag = true;
                out->entry_addr    = entry_tag->entry_addr;
                break;
            }

            case MB2_TAG_FRAMEBUFFER: {
                if (tag->size < sizeof(mb2_header_tag_framebuffer_t)) return false;
                const mb2_header_tag_framebuffer_t *fb_tag = (const mb2_header_tag_framebuffer_t *)tag;
                out->has_framebuffer      = true;
                out->framebuffer_optional = is_optional;
                out->fb_width             = fb_tag->width;
                out->fb_height            = fb_tag->height;
                out->fb_depth             = fb_tag->depth;
                break;
            }

            case MB2_TAG_MODULE_ALIGN:
                out->align_modules = true;
                break;

            case MB2_TAG_CONSOLE_FLAGS:
            case MB2_TAG_EFI_BS:
            case MB2_TAG_ENTRY_EFI32:
            case MB2_TAG_ENTRY_EFI64:
            case MB2_TAG_RELOCATABLE:
                // Valid Multiboot2 tags; safe to ignore on legacy BIOS unless non-optional
                break;

            default:
                // Per Multiboot2 spec: if an unknown tag is NOT marked optional, abort boot
                if (!is_optional) {
                    return false;
                }
                break;
        }

        // Each tag starts on an 8-byte aligned offset relative to the header start
        tag_offset += align_up_8(tag->size);
    }

    return true;
}