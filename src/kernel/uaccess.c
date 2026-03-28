#include "uaccess.h"
#include "errno.h"
#include "kstring.h"
#include "log.h"
#include "vmm.h"
#include "scheduler.h"

extern uint64_t kernel_hhdm_offset; // Assuming kernel_hhdm_offset is defined and available
extern pml4_t *kernel_pml4;

#define USERSPACE_END kernel_hhdm_offset

int validate_user_pointer(const void *ptr, size_t size) {
  uint64_t addr = (uint64_t)ptr;
  uint64_t end_addr = addr + size;

  // Check if pointer is in userspace range
  if (addr >= USERSPACE_END || (size > 0 && end_addr > USERSPACE_END)) {
    klog(LOG_WARN, "UACCESS: Pointer %p outside userspace (end: %p)", ptr,
         (void *)USERSPACE_END);
    return -EFAULT;
  }

  // Check for overflow
  if (size > 0 && end_addr < addr) {
    klog(LOG_WARN, "UACCESS: Address overflow for pointer %p, size %u", ptr,
         size);
    return -EFAULT;
  }

  // Check if pages are present and have user bit set
  pml4_t *pml4 = vmm_get_current_pml4();
  if (!pml4) return -EFAULT;

  for (uint64_t curr = addr & ~(PAGE_SIZE - 1); curr < end_addr; curr += PAGE_SIZE) {
      uint64_t pml4_index = (curr >> 39) & 0x1FF;
      uint64_t pdpt_index = (curr >> 30) & 0x1FF;
      uint64_t pd_index = (curr >> 21) & 0x1FF;
      uint64_t pt_index = (curr >> 12) & 0x1FF;

      if (!(pml4->entries[pml4_index] & PAGE_PRESENT)) return -EFAULT;
      pdpt_t *pdpt = (pdpt_t *)vmm_phys_to_virt((void *)(pml4->entries[pml4_index] & PHYSICAL_ADDR_MASK));
      
      if (!(pdpt->entries[pdpt_index] & PAGE_PRESENT)) return -EFAULT;
      pd_t *pd = (pd_t *)vmm_phys_to_virt((void *)(pdpt->entries[pdpt_index] & PHYSICAL_ADDR_MASK));
      
      if (!(pd->entries[pd_index] & PAGE_PRESENT)) return -EFAULT;
      pt_t *pt = (pt_t *)vmm_phys_to_virt((void *)(pd->entries[pd_index] & PHYSICAL_ADDR_MASK));
      
      if (!(pt->entries[pt_index] & PAGE_PRESENT)) return -EFAULT;
      if (!(pt->entries[pt_index] & PAGE_USER)) {
          klog(LOG_WARN, "UACCESS: Page at %p does not have USER bit set", (void*)curr);
          return -EFAULT;
      }
  }

  return 0;
}

int copy_from_user(void *kernel_dst, const void *user_src, size_t size) {
  if (size == 0) return 0;
  if (validate_user_pointer(user_src, size) != 0) {
    return -EFAULT;
  }

  // Basic "protection": we've already checked that all pages are present and user-accessible.
  // In a real OS, we would use a specialized assembly routine with an exception table.
  memcpy(kernel_dst, user_src, size);
  return 0;
}

int copy_to_user(void *user_dst, const void *kernel_src, size_t size) {
  if (size == 0) return 0;
  if (validate_user_pointer(user_dst, size) != 0) {
    return -EFAULT;
  }

  // Check if pages are writable
  pml4_t *pml4 = vmm_get_current_pml4();
  uint64_t addr = (uint64_t)user_dst;
  uint64_t end_addr = addr + size;
  for (uint64_t curr = addr & ~(PAGE_SIZE - 1); curr < end_addr; curr += PAGE_SIZE) {
      uint64_t pml4_index = (curr >> 39) & 0x1FF;
      uint64_t pdpt_index = (curr >> 30) & 0x1FF;
      uint64_t pd_index = (curr >> 21) & 0x1FF;
      uint64_t pt_index = (curr >> 12) & 0x1FF;

      pdpt_t *pdpt = (pdpt_t *)vmm_phys_to_virt((void *)(pml4->entries[pml4_index] & PHYSICAL_ADDR_MASK));
      pd_t *pd = (pd_t *)vmm_phys_to_virt((void *)(pdpt->entries[pdpt_index] & PHYSICAL_ADDR_MASK));
      pt_t *pt = (pt_t *)vmm_phys_to_virt((void *)(pd->entries[pd_index] & PHYSICAL_ADDR_MASK));
      
      if (!(pt->entries[pt_index] & PAGE_WRITE)) {
          klog(LOG_WARN, "UACCESS: Page at %p is not WRITABLE", (void*)curr);
          return -EFAULT;
      }
  }

  memcpy(user_dst, kernel_src, size);
  return 0;
}
