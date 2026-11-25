#include "tss.h"
#include "gdt.h"
#include "heap.h" // For kmalloc
#include "log.h"
#include <string.h> // For memset

#define KERNEL_TSS_STACK_SIZE 8192

static struct tss_entry_struct tss_entry;
static uint8_t
    tss_stack[KERNEL_TSS_STACK_SIZE] __attribute__((aligned(16))); // Static stack avoiding heap dependency

static uint8_t double_fault_stack[16384] __attribute__((aligned(16)));
static uint8_t nmi_stack[16384] __attribute__((aligned(16)));

// External assembly function to load the TSS register
extern void tss_flush();

void tss_init() {
  // No need to kmalloc, use static stack
  // tss_kernel_stack = (uint8_t*)kmalloc(KERNEL_TSS_STACK_SIZE);

  memset(&tss_entry, 0, sizeof(struct tss_entry_struct));

  // Set the kernel stack pointer for Ring 0
  tss_entry.rsp0 = (uint64_t)tss_stack + KERNEL_TSS_STACK_SIZE;

  // Emergency stacks remain usable even if the interrupted stack is corrupt
  tss_entry.ist1 = (uint64_t)double_fault_stack + sizeof(double_fault_stack);
  tss_entry.ist2 = (uint64_t)nmi_stack + sizeof(nmi_stack);
  tss_entry.iomap_base = sizeof(tss_entry);

  // Set up the TSS descriptor in the GDT
  gdt_set_tss(&tss_entry);

  // Load the TSS register (ltr)
  // The selector for TSS is the 5th entry in our GDT
  tss_flush();

  klog(LOG_INFO, "TSS Initialized.");
}

void tss_set_stack(uint64_t stack) { tss_entry.rsp0 = stack; }
