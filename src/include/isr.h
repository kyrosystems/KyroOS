#ifndef ISR_H
#define ISR_H

#include <stdint.h>

// CPU state pushed onto the stack by the ISR stub
struct registers {
  uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14,
      r15;
  uint64_t int_no, err_code;
  uint64_t rip, cs, rflags, rsp, ss;
};

void isr_handler(struct registers *regs);
void timer_init(uint32_t frequency);
uint64_t timer_get_ticks();

// IRQ handlers
typedef void (*irq_handler_t)(struct registers *regs);
void register_irq_handler(uint8_t irq, irq_handler_t handler);

static inline void enable_interrupts() { __asm__ __volatile__("sti"); }

static inline void disable_interrupts() { __asm__ __volatile__("cli"); }

static inline uint64_t read_cr2() {
  uint64_t val;
  __asm__ __volatile__("mov %%cr2, %0" : "=r"(val));
  return val;
}

static inline uint64_t read_cr3() {
  uint64_t val;
  __asm__ __volatile__("mov %%cr3, %0" : "=r"(val));
  return val;
}

#endif // ISR_H