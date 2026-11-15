#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// IDT entry structure for x86_64
struct idt_entry_struct {
   uint16_t base_low;
   uint16_t selector;
   uint8_t  ist;
   uint8_t  flags;
   uint16_t base_mid;
   uint32_t base_high;
   uint32_t reserved;
} __attribute__((packed));

// IDT pointer
struct idt_ptr_struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));


void idt_init();
void idt_load_ap(void);

#endif // IDT_H
