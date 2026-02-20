#include "uaccess.h"
#include "errno.h"
#include "kstring.h"
#include "log.h"
#include "vmm.h"

extern uint64_t kernel_hhdm_offset; // Assuming kernel_hhdm_offset is defined and available
extern pml4_t *kernel_pml4;

#define USERSPACE_END kernel_hhdm_offset

int validate_user_pointer(const void *ptr, size_t size) {
  uint64_t addr = (uint64_t)ptr;
  uint64_t end_addr = addr + size;

  // Check if pointer is in userspace range
  if (addr >= USERSPACE_END || end_addr >= USERSPACE_END) {
    klog(LOG_WARN, "UACCESS: Pointer %p outside userspace (end: %p)", ptr,
         (void *)USERSPACE_END);
    return -EFAULT;
  }

  // Check for overflow
  if (end_addr < addr) {
    klog(LOG_WARN, "UACCESS: Address overflow for pointer %p, size %u", ptr,
         size);
    return -EFAULT;
  }

  // TODO: Check if pages are present and have user bit set
  // This requires walking page tables, which we'll implement later

  return 0;
}

int copy_from_user(void *kernel_dst, const void *user_src, size_t size) {
  if (validate_user_pointer(user_src, size) != 0) {
    return -EFAULT;
  }

  // TODO: Add page fault protection around memcpy
  memcpy(kernel_dst, user_src, size);
  return 0;
}

int copy_to_user(void *user_dst, const void *kernel_src, size_t size) {
  if (validate_user_pointer(user_dst, size) != 0) {
    return -EFAULT;
  }

  // TODO: Add page fault protection around memcpy
  memcpy(user_dst, kernel_src, size);
  return 0;
}
