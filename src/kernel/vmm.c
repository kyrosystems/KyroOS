#include "vmm.h"
#include "kstring.h"
#include "log.h"
#include "pmm.h"
#include "panic.h" // For panic
#include <stddef.h> // for NULL

// Kernel's top-level page map (PML4)
pml4_t *kernel_pml4;
bool vmm_nx_supported = false;

#define IA32_EFER 0xC0000080
#define EFER_NXE  (1 << 11)

static void vmm_enable_nxe(void) {
  uint32_t eax, ebx, ecx, edx;
  __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001) : "memory");
  if (edx & (1 << 20)) { // CPU supports No-Execute bit
    uint32_t lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(IA32_EFER));
    lo |= EFER_NXE;
    __asm__ __volatile__("wrmsr" : : "a"(lo), "d"(hi), "c"(IA32_EFER));
    vmm_nx_supported = true;
  }
}

void *vmm_phys_to_virt(void *phys_addr) {
  return (void *)((uint64_t)phys_addr + kernel_hhdm_offset);
}

void *vmm_virt_to_phys(void *virt_addr) {
  return (void *)((uint64_t)virt_addr - kernel_hhdm_offset);
}

void vmm_init() {
  vmm_enable_nxe();
  uint64_t current_cr3_phys;
  __asm__ __volatile__("mov %%cr3, %0" : "=r"(current_cr3_phys));

  // 1. Allocate a new physical page for the writable PML4
  void *new_pml4_phys = pmm_alloc_page();
  if (!new_pml4_phys) {
    panic("VMM: Failed to allocate page for new writable PML4!", NULL);
  }

  // 2. Get the virtual address of the new PML4 (via HHDM)
  pml4_t *new_pml4_virt = (pml4_t *)vmm_phys_to_virt(new_pml4_phys);

  // 3. Clear the new PML4 entirely before copying.
  // This ensures a clean slate, especially for the user-space entries.
  memset(new_pml4_virt, 0, PAGE_SIZE);

  // 4. Copy the *upper half* (kernel mappings) from the old PML4 to the new writable PML4.
  // The old PML4 is at `current_cr3_phys`. We need its virtual address for memcpy.
  pml4_t *old_pml4_virt = (pml4_t *)vmm_phys_to_virt((void *)current_cr3_phys);
  memcpy(&new_pml4_virt->entries[256], &old_pml4_virt->entries[256],
         256 * sizeof(uint64_t));

  // 5. Update CR3 to point to this new writable PML4 physical address.
  // This makes the new PML4 active. After this, any page table operations
  // will use the new PML4.
  __asm__ __volatile__("mov %0, %%cr3" ::"r"(new_pml4_phys) : "memory");

  // 6. Set kernel_pml4 to the virtual address of this new writable PML4.
  kernel_pml4 = new_pml4_virt;

  klog(LOG_INFO, "VMM initialized. Kernel PML4 moved to writable %p (phys %p).", kernel_pml4, new_pml4_phys);
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
    void *new_pml4_phys = pmm_alloc_page();
    if (!new_pml4_phys) {
        klog(LOG_ERROR, "VMM: Failed to allocate page for new PML4.");
        return NULL;
    }

    pml4_t *new_pml4_virt = vmm_phys_to_virt(new_pml4_phys);
    memset(new_pml4_virt, 0, PAGE_SIZE);

    pml4_t *src = kernel_pml4 ? kernel_pml4 : vmm_get_current_pml4();
    memcpy(&new_pml4_virt->entries[256], &src->entries[256], 256 * sizeof(uint64_t));

    return new_pml4_virt;
}
void vmm_map_page(pml4_t *pml4_virt, void *virt, void *phys, uint64_t flags) {
    uint64_t virt_addr = (uint64_t)virt;
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_index   = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virt_addr >> 12) & 0x1FF;

    uint64_t table_flags = PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);

    pdpt_t *pdpt_virt;
    if (!(pml4_virt->entries[pml4_index] & PAGE_PRESENT)) {
        void *new_table_phys = pmm_alloc_page();
        if (!new_table_phys) panic("VMM: Out of memory for PDPT!", NULL);
        pdpt_virt = (pdpt_t *)vmm_phys_to_virt(new_table_phys);
        memset(pdpt_virt, 0, PAGE_SIZE);
        pml4_virt->entries[pml4_index] = (uint64_t)new_table_phys | table_flags;
    } else {
        pml4_virt->entries[pml4_index] |= table_flags;
        pdpt_virt = (pdpt_t *)vmm_phys_to_virt(
            (void *)(pml4_virt->entries[pml4_index] & PHYSICAL_ADDR_MASK));
    }

    pd_t *pd_virt;
    if (!(pdpt_virt->entries[pdpt_index] & PAGE_PRESENT)) {
        void *new_table_phys = pmm_alloc_page();
        if (!new_table_phys) panic("VMM: Out of memory for PD!", NULL);
        pd_virt = (pd_t *)vmm_phys_to_virt(new_table_phys);
        memset(pd_virt, 0, PAGE_SIZE);
        pdpt_virt->entries[pdpt_index] = (uint64_t)new_table_phys | table_flags;
    } else {
        pdpt_virt->entries[pdpt_index] |= table_flags;
        pd_virt = (pd_t *)vmm_phys_to_virt(
            (void *)(pdpt_virt->entries[pdpt_index] & PHYSICAL_ADDR_MASK));
    }

    pt_t *pt_virt;
    if (!(pd_virt->entries[pd_index] & PAGE_PRESENT)) {
        void *new_table_phys = pmm_alloc_page();
        if (!new_table_phys) panic("VMM: Out of memory for PT!", NULL);
        pt_virt = (pt_t *)vmm_phys_to_virt(new_table_phys);
        memset(pt_virt, 0, PAGE_SIZE);
        pd_virt->entries[pd_index] = (uint64_t)new_table_phys | table_flags;
    } else {
        pd_virt->entries[pd_index] |= table_flags;
        pt_virt = (pt_t *)vmm_phys_to_virt(
            (void *)(pd_virt->entries[pd_index] & PHYSICAL_ADDR_MASK));
    }

    pt_virt->entries[pt_index] = (uint64_t)phys | flags;
    __asm__ __volatile__("invlpg (%0)" :: "r"(virt_addr) : "memory");
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
      (void *)(pdpt_virt->entries[pdpt_index] & PHYSICAL_ADDR_MASK)); // Fixed here
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
      (void *)(pdpt->entries[pdpt_index] & PHYSICAL_ADDR_MASK)); // Fixed here

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

    void *kernel_virt_dest = vmm_phys_to_virt((void *)phys_dest);
    memcpy(kernel_virt_dest, src_ptr, copy_size);

    virt_addr += copy_size;
    src_ptr += copy_size;
    remaining -= copy_size;
  }
}

#include "fb.h"

void vmm_destroy_address_space(pml4_t *pml4) {
  if (!pml4 || pml4 == kernel_pml4 || pml4 == vmm_get_current_pml4())
    return;

  const fb_info_t *fb = fb_get_info();
  uint64_t fb_phys = (fb && fb->address) ? ((uint64_t)fb->address - kernel_hhdm_offset) : 0;
  uint64_t fb_len = fb ? ((uint64_t)fb->pitch * fb->height) : 0;

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
              for (int m = 0; m < 512; m++) {
                if (pt->entries[m] & PAGE_PRESENT) {
                  uint64_t phys = pt->entries[m] & PHYSICAL_ADDR_MASK;
                  // Only free physical pages that belong to userspace allocations (not MMIO or Framebuffer)
                  if ((pt->entries[m] & (PAGE_USER | PAGE_OWNED)) &&
                      (!fb_phys || phys < fb_phys || phys >= fb_phys + fb_len)) {
                    pmm_free_page((void *)phys);
                  }
                  pt->entries[m] = 0;
                }
              }
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

pml4_t *vmm_clone_address_space(pml4_t *src_pml4) {
    if (!src_pml4) return NULL;
    void *new_pml4_phys = pmm_alloc_page();
    if (!new_pml4_phys) return NULL;

    pml4_t *new_pml4_virt = (pml4_t *)vmm_phys_to_virt(new_pml4_phys);
    memset(new_pml4_virt, 0, PAGE_SIZE);

    // Copy kernel space (upper half: 256..511)
    pml4_t *k = kernel_pml4 ? kernel_pml4 : vmm_get_current_pml4();
    memcpy(&new_pml4_virt->entries[256], &k->entries[256], 256 * sizeof(uint64_t));

    // Clone user space (lower half: 0..255) with COW
    for (int pml4_i = 0; pml4_i < 256; pml4_i++) {
        if (!(src_pml4->entries[pml4_i] & PAGE_PRESENT)) continue;

        void *new_pdpt_phys = pmm_alloc_page();
        if (!new_pdpt_phys) return NULL;
        pdpt_t *new_pdpt_virt = (pdpt_t *)vmm_phys_to_virt(new_pdpt_phys);
        memset(new_pdpt_virt, 0, PAGE_SIZE);
        new_pml4_virt->entries[pml4_i] = (uint64_t)new_pdpt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

        pdpt_t *src_pdpt_virt = (pdpt_t *)vmm_phys_to_virt((void *)(src_pml4->entries[pml4_i] & PHYSICAL_ADDR_MASK));
        for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
            if (!(src_pdpt_virt->entries[pdpt_i] & PAGE_PRESENT)) continue;

            void *new_pd_phys = pmm_alloc_page();
            if (!new_pd_phys) return NULL;
            pd_t *new_pd_virt = (pd_t *)vmm_phys_to_virt(new_pd_phys);
            memset(new_pd_virt, 0, PAGE_SIZE);
            new_pdpt_virt->entries[pdpt_i] = (uint64_t)new_pd_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

            pd_t *src_pd_virt = (pd_t *)vmm_phys_to_virt((void *)(src_pdpt_virt->entries[pdpt_i] & PHYSICAL_ADDR_MASK));
            for (int pd_i = 0; pd_i < 512; pd_i++) {
                if (!(src_pd_virt->entries[pd_i] & PAGE_PRESENT)) continue;

                void *new_pt_phys = pmm_alloc_page();
                if (!new_pt_phys) return NULL;
                pt_t *new_pt_virt = (pt_t *)vmm_phys_to_virt(new_pt_phys);
                memset(new_pt_virt, 0, PAGE_SIZE);
                new_pd_virt->entries[pd_i] = (uint64_t)new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;

                pt_t *src_pt_virt = (pt_t *)vmm_phys_to_virt((void *)(src_pd_virt->entries[pd_i] & PHYSICAL_ADDR_MASK));
                for (int pt_i = 0; pt_i < 512; pt_i++) {
                    uint64_t pte = src_pt_virt->entries[pt_i];
                    if (!(pte & PAGE_PRESENT)) continue;

                    uint64_t phys = pte & PHYSICAL_ADDR_MASK;
                    // If writable user page, mark COW and remove write permissions in both
                    if (pte & PAGE_WRITE) {
                        pte &= ~PAGE_WRITE;
                        pte |= PAGE_COW;
                        src_pt_virt->entries[pt_i] = pte;
                    }
                    new_pt_virt->entries[pt_i] = pte;
                    pmm_page_ref((void *)phys);
                }
            }
        }
    }

    // Flush current TLB to activate read-only protection in parent
    uint64_t cur_cr3;
    __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cur_cr3) :: "memory");

    return new_pml4_virt;
}

bool vmm_handle_page_fault(uint64_t fault_addr, uint64_t err_code) {
    // Only handle write faults (bit 1 of err_code is set)
    if (!(err_code & 2)) return false;

    pml4_t *pml4 = vmm_get_current_pml4();
    uint64_t pml4_i = (fault_addr >> 39) & 0x1FF;
    uint64_t pdpt_i = (fault_addr >> 30) & 0x1FF;
    uint64_t pd_i   = (fault_addr >> 21) & 0x1FF;
    uint64_t pt_i   = (fault_addr >> 12) & 0x1FF;

    if (!(pml4->entries[pml4_i] & PAGE_PRESENT)) return false;
    pdpt_t *pdpt = (pdpt_t *)vmm_phys_to_virt((void *)(pml4->entries[pml4_i] & PHYSICAL_ADDR_MASK));
    if (!(pdpt->entries[pdpt_i] & PAGE_PRESENT)) return false;
    pd_t *pd = (pd_t *)vmm_phys_to_virt((void *)(pdpt->entries[pdpt_i] & PHYSICAL_ADDR_MASK));
    if (!(pd->entries[pd_i] & PAGE_PRESENT)) return false;
    pt_t *pt = (pt_t *)vmm_phys_to_virt((void *)(pd->entries[pd_i] & PHYSICAL_ADDR_MASK));
    if (!(pt->entries[pt_i] & PAGE_PRESENT)) return false;

    uint64_t pte = pt->entries[pt_i];
    if (!(pte & PAGE_COW)) return false; // Not a COW fault

    uint64_t old_phys = pte & PHYSICAL_ADDR_MASK;
    uint16_t ref = pmm_page_get_ref((void *)old_phys);

    if (ref <= 1) {
        // Sole owner: restore write permission directly
        pte &= ~PAGE_COW;
        pte |= PAGE_WRITE;
        pt->entries[pt_i] = pte;
    } else {
        // Shared COW page: allocate new physical page, copy data, map writable
        void *new_phys = pmm_alloc_page();
        if (!new_phys) return false;
        memcpy(vmm_phys_to_virt(new_phys), vmm_phys_to_virt((void *)old_phys), PAGE_SIZE);

        pte &= ~PHYSICAL_ADDR_MASK;
        pte &= ~PAGE_COW;
        pte |= (uint64_t)new_phys | PAGE_WRITE;
        pt->entries[pt_i] = pte;

        pmm_page_unref((void *)old_phys);
    }

    uint64_t aligned_vaddr = fault_addr & ~0xFFFULL;
    __asm__ volatile("invlpg (%0)" :: "r"(aligned_vaddr) : "memory");
    return true;
}
