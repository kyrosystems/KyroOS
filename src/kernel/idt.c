#include "idt.h"
#include "isr.h"
#include "log.h"
#include "port_io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

static struct idt_entry_struct idt_entries[256];
static struct idt_ptr_struct idt_ptr;

// External assembly stubs for ISRs
extern void isr0(), isr1(), isr2(), isr3(), isr4(), isr5(), isr6(), isr7(),
    isr8(), isr9(), isr10(), isr11(), isr12(), isr13(), isr14(), isr15();
extern void isr16(), isr17(), isr18(), isr19(), isr20(), isr21(), isr22(),
    isr23(), isr24(), isr25(), isr26(), isr27(), isr28(), isr29(), isr30(),
    isr31();
extern void irq0(), irq1(), irq2(), irq3(), irq4(), irq5(), irq6(), irq7(),
    irq8(), irq9(), irq10(), irq11(), irq12(), irq13(), irq14(), irq15();
extern void isr128();

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel,
                         uint8_t flags) {
  idt_entries[num].base_low = (uint16_t)(base & 0xFFFF);
  idt_entries[num].base_mid = (uint16_t)((base >> 16) & 0xFFFF);
  idt_entries[num].base_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
  idt_entries[num].selector = sel;
  idt_entries[num].ist = 0;
  idt_entries[num].flags = flags;
  idt_entries[num].reserved = 0;
}

static void pic_remap(void) {
  outb(PIC1_COMMAND, 0x11);
  outb(PIC2_COMMAND, 0x11);
  outb(PIC1_DATA, 0x20);
  outb(PIC2_DATA, 0x28);
  outb(PIC1_DATA, 0x04);
  outb(PIC2_DATA, 0x02);
  outb(PIC1_DATA, 0x01);
  outb(PIC2_DATA, 0x01);
  outb(PIC1_DATA, 0x0);
  outb(PIC2_DATA, 0x0);
}

void idt_init() {
  klog(LOG_INFO, "IDT: Initializing...");
  idt_ptr.limit = (sizeof(struct idt_entry_struct) * 256) - 1;
  idt_ptr.base = (uint64_t)&idt_entries;

  for (int i = 0; i < 256; i++) {
    idt_set_gate(i, 0, 0, 0);
  }

  const uint16_t KERNEL_CS = 0x08;
  const uint8_t KERNEL_GATE_FLAGS = 0x8E;
  const uint8_t USER_GATE_FLAGS = 0xEE;

  idt_set_gate(0, (uint64_t)isr0, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(1, (uint64_t)isr1, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(2, (uint64_t)isr2, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(3, (uint64_t)isr3, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(4, (uint64_t)isr4, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(5, (uint64_t)isr5, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(6, (uint64_t)isr6, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(7, (uint64_t)isr7, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(8, (uint64_t)isr8, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(9, (uint64_t)isr9, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(10, (uint64_t)isr10, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(11, (uint64_t)isr11, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(12, (uint64_t)isr12, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(13, (uint64_t)isr13, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(14, (uint64_t)isr14, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(15, (uint64_t)isr15, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(16, (uint64_t)isr16, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(17, (uint64_t)isr17, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(18, (uint64_t)isr18, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(19, (uint64_t)isr19, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(20, (uint64_t)isr20, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(21, (uint64_t)isr21, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(22, (uint64_t)isr22, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(23, (uint64_t)isr23, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(24, (uint64_t)isr24, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(25, (uint64_t)isr25, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(26, (uint64_t)isr26, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(27, (uint64_t)isr27, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(28, (uint64_t)isr28, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(29, (uint64_t)isr29, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(30, (uint64_t)isr30, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(31, (uint64_t)isr31, KERNEL_CS, KERNEL_GATE_FLAGS);

  pic_remap();

  idt_set_gate(32, (uint64_t)irq0, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(33, (uint64_t)irq1, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(34, (uint64_t)irq2, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(35, (uint64_t)irq3, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(36, (uint64_t)irq4, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(37, (uint64_t)irq5, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(38, (uint64_t)irq6, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(39, (uint64_t)irq7, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(40, (uint64_t)irq8, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(41, (uint64_t)irq9, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(42, (uint64_t)irq10, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(43, (uint64_t)irq11, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(44, (uint64_t)irq12, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(45, (uint64_t)irq13, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(46, (uint64_t)irq14, KERNEL_CS, KERNEL_GATE_FLAGS);
  idt_set_gate(47, (uint64_t)irq15, KERNEL_CS, KERNEL_GATE_FLAGS);

  idt_set_gate(128, (uint64_t)isr128, KERNEL_CS, USER_GATE_FLAGS);

  __asm__ __volatile__("lidt %0" : : "m"(idt_ptr));
  klog(LOG_INFO, "IDT: Loaded successfully.");
}