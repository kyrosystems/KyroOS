#ifndef VMM_H
#define VMM_H

#include <stddef.h>
#include <stdint.h>


#define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000
#define PAGE_PRESENT (1 << 0)
#define PAGE_WRITE (1 << 1)
#define PAGE_USER (1 << 2)
#define PAGE_NO_EXEC (1ULL << 63) // No-Execute bit (NX)
#define PHYSICAL_ADDR_MASK 0x000FFFFFFFFFF000ULL

extern uint64_t kernel_hhdm_offset; // Defined in kernel.c

// Function declarations for P_TO_V and V_TO_P equivalents
void *vmm_phys_to_virt(void *phys_addr);
void *vmm_virt_to_phys(void *virt_addr);

// Page Map Level 4
typedef struct {
  uint64_t entries[512];
} pml4_t;

// Page Directory Pointer Table
typedef struct {
  uint64_t entries[512];
} pdpt_t;

// Page Directory
typedef struct {
  uint64_t entries[512];
} pd_t;

// Page Table
typedef struct {
  uint64_t entries[512];
} pt_t;

#define PAGE_SIZE 4096

// Userspace stack definitions
#define USER_STACK_TOP 0x00007FFFFFFFF000ULL
#define USER_STACK_SIZE (256 * PAGE_SIZE) // 1MB userspace stack

extern pml4_t *kernel_pml4; // Declared here, defined in vmm.c

void vmm_init();
void vmm_map_page(pml4_t *pml4, void *virt, void *phys, uint64_t flags);
void *vmm_unmap_page(pml4_t *pml4, void *virt);
void vmm_map_page_current(void *virt, void *phys, uint64_t flags);
void *vmm_unmap_page_current(void *virt);
pml4_t *vmm_create_address_space();
void vmm_destroy_address_space(pml4_t *pml4);
void vmm_switch_address_space(pml4_t *pml4);
pml4_t *vmm_get_current_pml4();
uint64_t vmm_get_phys_addr(pml4_t *pml4, void *vaddr);
void vmm_memcpy_to_userspace(pml4_t *pml4_target, void *dest_vaddr,
                             const void *src, size_t n);

#endif // VMM_H
