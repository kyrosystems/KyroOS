#include "vmm.h"
#include "kstring.h"
#include "log.h"
#include "pmm.h"
#include <stddef.h> // for NULL

// Kernel's top-level page map (PML4)
pml4_t *kernel_pml4;

void *vmm_phys_to_virt(void *phys_addr) {
  return (void *)((uint64_t)phys_addr + kernel_hhdm_offset);
}

void *vmm_virt_to_phys(void *virt_addr) {
  return (void *)((uint64_t)virt_addr - kernel_hhdm_offset);
}

void vmm_init() {
  uint64_t current_cr3_phys;
  __asm__ __volatile__("mov %%cr3, %0" : "=r"(current_cr3_phys));
  kernel_pml4 = (pml4_t *)vmm_phys_to_virt((void *)current_cr3_phys);
  klog(LOG_INFO, "VMM initialized. Kernel PML4 at %p.", kernel_pml4);
}

pml4_t *vmm_get_current_pml4() {
  uint64_t pml4_phys;
  __asm__ __volatile__("mov %%cr3, %0" : "=r"(pml4_phys));
  return (pml4_t *)vmm_phys_to_virt((void *)pml4_phys);
}

void vmm_switch_address_space(pml4_t *pml4) {
  uint64_t pml4_phys = (uint64_t)vmm_virt_to_phys(pml4);
  __asm__ __volatile__("mov %0, %%cr3" ::"r"(pml4_phys) : "memory");
}

pml4_t *vmm_create_address_space() {
  pml4_t *new_pml4_virt = (pml4_t *)vmm_phys_to_virt(pmm_alloc_page());
  if (!new_pml4_virt) {
    klog(LOG_ERROR, "VMM: Failed to allocate page for new PML4.");
    return NULL;
  }

  // Clear the lower (user) half of the new page map
  memset(new_pml4_virt, 0, 256 * sizeof(uint64_t));

  // Copy the upper (kernel) half of the page map
  memcpy(&new_pml4_virt->entries[256], &kernel_pml4->entries[256],
         256 * sizeof(uint64_t));

  return new_pml4_virt;
}

void vmm_map_page(pml4_t *pml4_virt, void *virt, void *phys, uint64_t flags) {
  uint64_t virt_addr = (uint64_t)virt;
  uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
  uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
  uint64_t pd_index = (virt_addr >> 21) & 0x1FF;
  uint64_t pt_index = (virt_addr >> 12) & 0x1FF;

  pdpt_t *pdpt_virt;
  if (!(pml4_virt->entries[pml4_index] & PAGE_PRESENT)) {
    void *new_table_phys = pmm_alloc_page();
    if (!new_table_phys)
      panic("VMM: Out of memory for PDPT!", NULL);
    pdpt_virt = (pdpt_t *)vmm_phys_to_virt((void *)new_table_phys);
    memset(pdpt_virt, 0, PAGE_SIZE);
    pml4_virt->entries[pml4_index] =
        (uint64_t)new_table_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
  } else {
    pdpt_virt = (pdpt_t *)vmm_phys_to_virt(
        (void *)(pml4_virt->entries[pml4_index] & PHYSICAL_ADDR_MASK));
  }

  pd_t *pd_virt;
  if (!(pdpt_virt->entries[pdpt_index] & PAGE_PRESENT)) {
    void *new_table_phys = pmm_alloc_page();
    if (!new_table_phys)
      panic("VMM: Out of memory for PD!", NULL);
    pd_virt = (pd_t *)vmm_phys_to_virt((void *)new_table_phys);
    memset(pd_virt, 0, PAGE_SIZE);
    pdpt_virt->entries[pdpt_index] =
        (uint64_t)new_table_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
  } else {
    pd_virt = (pd_t *)vmm_phys_to_virt(
        (void *)(pdpt_virt->entries[pdpt_index] & PHYSICAL_ADDR_MASK));
  }

  pt_t *pt_virt;
  if (!(pd_virt->entries[pd_index] & PAGE_PRESENT)) {
    void *new_table_phys = pmm_alloc_page();
    if (!new_table_phys)
      panic("VMM: Out of memory for PT!", NULL);
    pt_virt = (pt_t *)vmm_phys_to_virt((void *)new_table_phys);
    memset(pt_virt, 0, PAGE_SIZE);
    pd_virt->entries[pd_index] =
        (uint64_t)new_table_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
  } else {
    pt_virt = (pt_t *)vmm_phys_to_virt(
        (void *)(pd_virt->entries[pd_index] & PHYSICAL_ADDR_MASK));
  }

  pt_virt->entries[pt_index] = (uint64_t)phys | flags;

  __asm__ __volatile__("invlpg (%0)" ::"r"(virt_addr) : "memory");
}

void vmm_map_page_current(void *virt, void *phys, uint64_t flags) {
  vmm_map_page(vmm_get_current_pml4(), virt, phys, flags);
}

void *vmm_unmap_page(pml4_t *pml4_virt, void *virt) {
  uint64_t virt_addr = (uint64_t)virt;
  uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
  uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
  uint64_t pd_index = (virt_addr >> 21) & 0x1FF;
  uint64_t pt_index = (virt_addr >> 12) & 0x1FF;

  if (!(pml4_virt->entries[pml4_index] & PAGE_PRESENT))
    return NULL;

  pdpt_t *pdpt_virt = (pdpt_t *)vmm_phys_to_virt(
      (void *)(pml4_virt->entries[pml4_index] & PHYSICAL_ADDR_MASK));
  if (!(pdpt_virt->entries[pdpt_index] & PAGE_PRESENT))
    return NULL;

  pd_t *pd_virt = (pd_t *)vmm_phys_to_virt(
      (void *)(pdpt_virt->entries[pdpt_index] & PHYSICAL_ADDR_MASK));
  if (!(pd_virt->entries[pd_index] & PAGE_PRESENT))
    return NULL;

  pt_t *pt_virt = (pt_t *)vmm_phys_to_virt(
      (void *)(pd_virt->entries[pd_index] & PHYSICAL_ADDR_MASK));
  if (!(pt_virt->entries[pt_index] & PAGE_PRESENT))
    return NULL;

  void *phys_addr = (void *)(pt_virt->entries[pt_index] & PHYSICAL_ADDR_MASK);
  pt_virt->entries[pt_index] = 0;
  __asm__ __volatile__("invlpg (%0)" ::"r"(virt_addr) : "memory");
  return phys_addr;
}

void *vmm_unmap_page_current(void *virt) {
  return vmm_unmap_page(vmm_get_current_pml4(), virt);
}

uint64_t vmm_get_phys_addr(pml4_t *pml4, void *vaddr) {
  uint64_t virt_addr = (uint64_t)vaddr;
  uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
  uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
  uint64_t pd_index = (virt_addr >> 21) & 0x1FF;
  uint64_t pt_index = (virt_addr >> 12) & 0x1FF;
  uint64_t page_offset = virt_addr & 0xFFF;

  if (!(pml4->entries[pml4_index] & PAGE_PRESENT))
    return 0;
  pdpt_t *pdpt = (pdpt_t *)vmm_phys_to_virt(
      (void *)(pml4->entries[pml4_index] & PHYSICAL_ADDR_MASK));

  if (!(pdpt->entries[pdpt_index] & PAGE_PRESENT))
    return 0;
  pd_t *pd = (pd_t *)vmm_phys_to_virt(
      (void *)(pdpt->entries[pdpt_index] & PHYSICAL_ADDR_MASK));

  if (!(pd->entries[pd_index] & PAGE_PRESENT))
    return 0;
  pt_t *pt = (pt_t *)vmm_phys_to_virt(
      (void *)(pd->entries[pd_index] & PHYSICAL_ADDR_MASK));

  if (!(pt->entries[pt_index] & PAGE_PRESENT))
    return 0;
  return (pt->entries[pt_index] & PHYSICAL_ADDR_MASK) + page_offset;
}

void vmm_memcpy_to_userspace(pml4_t *pml4_target, void *dest_vaddr,
                             const void *src, size_t n) {
  uint64_t virt_addr = (uint64_t)dest_vaddr;
  uint8_t *src_ptr = (uint8_t *)src;
  size_t remaining = n;

  while (remaining > 0) {
    uint64_t page_offset = virt_addr % PAGE_SIZE;
    uint64_t copy_size = PAGE_SIZE - page_offset;
    if (copy_size > remaining) {
      copy_size = remaining;
    }

    uint64_t phys_dest = vmm_get_phys_addr(pml4_target, (void *)virt_addr);
    if (phys_dest == 0) {
      klog(LOG_ERROR,
           "vmm_memcpy_to_userspace: vaddr %lx not mapped in PML4 %p",
           virt_addr, pml4_target);
      return;
    }

    klog(LOG_DEBUG,
         "vmm_memcpy_to_userspace: copying %d bytes to vaddr %lx (phys %lx)",
         (int)copy_size, virt_addr, phys_dest);
    void *kernel_virt_dest = vmm_phys_to_virt((void *)phys_dest);
    memcpy(kernel_virt_dest, src_ptr, copy_size);

    virt_addr += copy_size;
    src_ptr += copy_size;
    remaining -= copy_size;
  }
}

void vmm_destroy_address_space(pml4_t *pml4) {
  if (!pml4)
    return;

  for (int i = 0; i < 256; i++) {
    if (pml4->entries[i] & PAGE_PRESENT) {
      pdpt_t *pdpt = (pdpt_t *)vmm_phys_to_virt(
          (void *)(pml4->entries[i] & PHYSICAL_ADDR_MASK));
      for (int j = 0; j < 512; j++) {
        if (pdpt->entries[j] & PAGE_PRESENT) {
          pd_t *pd = (pd_t *)vmm_phys_to_virt(
              (void *)(pdpt->entries[j] & PHYSICAL_ADDR_MASK));
          for (int k = 0; k < 512; k++) {
            if (pd->entries[k] & PAGE_PRESENT) {
              pt_t *pt = (pt_t *)vmm_phys_to_virt(
                  (void *)(pd->entries[k] & PHYSICAL_ADDR_MASK));
              pmm_free_page(vmm_virt_to_phys((void *)pt));
            }
          }
          pmm_free_page(vmm_virt_to_phys((void *)pd));
        }
      }
      pmm_free_page(vmm_virt_to_phys((void *)pdpt));
    }
  }
  pmm_free_page(vmm_virt_to_phys((void *)pml4));
}
