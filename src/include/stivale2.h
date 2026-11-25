#ifndef __STIVALE2_H__
#define __STIVALE2_H__

#include <stdint.h>

// This is a tramp stamp for the stivale2 boot protocol.
#define STIVALE2_TRAMP_STAMP 0x506461d296a07c7e

// stivale2 tags
#define STIVALE2_HEADER_TAG_FRAMEBUFFER_ID 0x3ecc1bc43d0f7971
#define STIVALE2_STRUCT_TAG_FRAMEBUFFER_ID 0x506461d296a07c7e
#define STIVALE2_STRUCT_TAG_MEMMAP_ID      0x2187f79e8612de07

#define STIVALE2_MMAP_USABLE 1

struct stivale2_tag {
    uint64_t identifier;
    uint64_t next;
};

struct stivale2_struct {
    char bootloader_brand[64];
    char bootloader_version[64];
    uint64_t tags;
};

struct stivale2_struct_tag_framebuffer {
    struct stivale2_tag tag;
    uint64_t framebuffer_addr;
    uint16_t framebuffer_width;
    uint16_t framebuffer_height;
    uint16_t framebuffer_pitch;
    uint16_t framebuffer_bpp;
    uint8_t  memory_model;
    uint8_t  red_mask_size;
    uint8_t  red_mask_shift;
    uint8_t  green_mask_size;
    uint8_t  green_mask_shift;
    uint8_t  blue_mask_size;
    uint8_t  blue_mask_shift;
};

struct stivale2_mmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t unused;
};

struct stivale2_struct_tag_memmap {
    struct stivale2_tag tag;
    uint64_t entries;
    struct stivale2_mmap_entry memmap[];
};

#endif
