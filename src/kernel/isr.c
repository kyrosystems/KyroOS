#include "isr.h"
#include "log.h"
#include "port_io.h"
#include "scheduler.h"
#include "panic_screen.h"

extern void
syscall_handler(struct registers *regs); // Declare syscall_handler here

// Array of IRQ handlers
static irq_handler_t irq_handlers[16] = {0};

// Exception messages
static const char *exception_messages[] = {"Division By Zero",
                                           "Debug",
                                           "Non-Maskable Interrupt",
                                           "Breakpoint",
                                           "Overflow",
                                           "Bound Range Exceeded",
                                           "Invalid Opcode",
                                           "Device Not Available",
                                           "Double Fault",
                                           "Coprocessor Segment Overrun",
                                           "Invalid TSS",
                                           "Segment Not Present",
                                           "Stack-Segment Fault",
                                           "General Protection Fault",
                                           "Page Fault",
                                           "Reserved",
                                           "x87 FPU Error",
                                           "Alignment Check",
                                           "Machine Check",
                                           "SIMD Floating-Point Exception",
                                           "Virtualization Exception",
                                           "Control Protection Exception",
                                           "Reserved",
                                           "Reserved",
                                           "Reserved",
                                           "Reserved",
                                           "Reserved",
                                           "Reserved",
                                           "Reserved",
                                           "Reserved",
                                           "Reserved",
                                           "Reserved"};

static uint64_t ticks = 0;

uint64_t timer_get_ticks() { return ticks; }

void timer_init(uint32_t frequency) {
  uint32_t divisor = 1193180 / frequency;

  // Send the command byte.
  outb(0x43, 0x36);

  // Divisor has to be sent byte-wise, so split here into upper/lower bytes.
  uint8_t l = (uint8_t)(divisor & 0xFF);
  uint8_t h = (uint8_t)((divisor >> 8) & 0xFF);

  // Send the frequency divisor.
  outb(0x40, l);
  outb(0x40, h);
}

void isr_handler(struct registers *regs) {
  if (regs->int_no < 32) { // CPU Exceptions
    klog(LOG_ERROR, "EXCEPTION: %s (#%d) at RIP: %p, Error Code: %lx",
         exception_messages[regs->int_no], regs->int_no, (void *)regs->rip,
         regs->err_code);

    // Direct serial print for emergency debugging
    serial_print("\n--- EMERGENCY EXCEPTION DUMP ---\n");
    serial_print("Exception: ");
    serial_print(exception_messages[regs->int_no]);
    serial_print("\n");
    serial_print("RIP: ");
    serial_print_hex(regs->rip);
    serial_print("\n");
    serial_print("RAX: ");
    serial_print_hex(regs->rax);
    serial_print(" RBX: ");
    serial_print_hex(regs->rbx);
    serial_print("\n");
    serial_print("RCX: ");
    serial_print_hex(regs->rcx);
    serial_print(" RDX: ");
    serial_print_hex(regs->rdx);
    serial_print("\n");
    serial_print("RSP: ");
    serial_print_hex(regs->rsp);
    serial_print(" ERR: ");
    serial_print_hex(regs->err_code);
    serial_print("\n");

    klog(LOG_ERROR,
         "Registers: RAX=%lx, RBX=%lx, RCX=%lx, RDX=%lx, RSP=%lx, RFLAGS=%lx",
         regs->rax, regs->rbx, regs->rcx, regs->rdx, regs->rsp, regs->rflags);
    panic_screen_show(exception_messages[regs->int_no], regs);
    for (;;) __asm__ volatile("cli; hlt");
  } else if (regs->int_no >= 32 && regs->int_no <= 47) { // IRQs
    uint8_t irq_num = regs->int_no - 32;

    if (irq_num == 0) {
      ticks++;
    }

    if (irq_handlers[irq_num] != 0) {
      irq_handlers[irq_num](regs);
    }

    // Send EOI (End Of Interrupt) to PIC before scheduling
    if (irq_num >= 8) {
      outb(0xA0, 0x20); // Send EOI to slave
    }
    outb(0x20, 0x20); // Send EOI to master

    // Call the scheduler on timer interrupt
    if (irq_num == 0) {
      schedule();
    }
  } else if (regs->int_no == 128) { // Syscall interrupt (0x80)
    syscall_handler(regs);
  }
}

void register_irq_handler(uint8_t irq, irq_handler_t handler) {
  if (irq < 16) {
    irq_handlers[irq] = handler;
  }
}
