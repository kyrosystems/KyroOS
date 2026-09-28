#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// GDT entry structure
struct gdt_entry_bits {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

// GDT pointer
struct gdt_ptr_struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// System segment descriptor for TSS
struct gdt_system_entry_bits {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));


void gdt_init();
void gdt_set_tss(void* tss_base);

#endif // GDT_H