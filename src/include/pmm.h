#ifndef PMM_H
#define PMM_H

#include "limine.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))

// Initialize the physical memory manager
void pmm_init(struct limine_memmap_response *mmap_response,
              uint64_t hhdm_offset);

// Allocate a single physical page
void *pmm_alloc_page();

// Allocate multiple contiguous physical pages
void *pmm_alloc_pages(size_t count);

// Free a single physical page
void pmm_free_page(void *p);

// Convert physical to virtual using HHDM
void *p_to_v(void *p);

// Get memory stats (in bytes)
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_used_memory(void);

#endif // PMM_H
