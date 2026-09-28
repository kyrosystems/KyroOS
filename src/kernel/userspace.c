#include "userspace.h"
#include "gdt.h"
#include "vmm.h"
#include "pmm.h"
#include "log.h"
#include <stddef.h> // For NULL

// Selectors from GDT
#define USER_CODE_SELECTOR 0x18 | 3 // Ring 3
#define USER_DATA_SELECTOR 0x20 | 3 // Ring 3

#define USER_STACK_VADDR 0x70000000
#define USER_STACK_PAGES 4

void enter_userspace(uint64_t entry_point) {
    klog(LOG_INFO, "Preparing to enter userspace...");

    // Allocate stack for the userspace program
    for (int i = 0; i < USER_STACK_PAGES; i++) {
        void* page = pmm_alloc_page();
        if (!page) {
            panic("Failed to allocate user stack!", NULL);
        }
        vmm_map_page_current((void*)(uintptr_t)(USER_STACK_VADDR + i * PAGE_SIZE), page, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
    
    // Set up the stack for iretq
    uint64_t user_rsp = USER_STACK_VADDR + (USER_STACK_PAGES * PAGE_SIZE);
    klog(LOG_INFO, "User stack allocated at vaddr %p, user_rsp = %p", (void*)USER_STACK_VADDR, (void*)user_rsp);

    __asm__ __volatile__(
        "cli\n\t"
        "pushq %0\n\t"          // SS
        "pushq %1\n\t"          // RSP
        "pushq %2\n\t"          // RFLAGS (with IF bit set)
        "pushq %3\n\t"          // CS
        "pushq %4\n\t"          // RIP
        "iretq"
        : 
        : "i"(USER_DATA_SELECTOR), "r"(user_rsp), "i"(0x202), "i"(USER_CODE_SELECTOR), "r"(entry_point)
        : "memory"
    );
}