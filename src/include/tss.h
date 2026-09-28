#ifndef TSS_H
#define TSS_H

#include <stdint.h>

// Task State Segment for x86_64
struct tss_entry_struct {
    uint32_t reserved1;
    uint64_t rsp0; // Kernel stack pointer for ring 0
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iomap_base;
} __attribute__((packed));

void tss_init();
void tss_set_stack(uint64_t stack);

#endif // TSS_H
