#include "pmm.h"
#include "kstring.h"
#include "log.h"
// #include "stivale2.h"

static uint8_t *pmm_bitmap = NULL;
static uint64_t pmm_total_pages = 0;
static uint64_t pmm_usable_pages = 0;
static uint64_t pmm_allocated_pages = 0; // NEW: track actual allocations
static uint64_t pmm_last_free_page = 0;
static uint64_t pmm_hhdm_offset = 0;

// External symbols from linker script are still useful for marking the kernel
// itself.
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];
extern uint8_t _boot_end[]; // Physical end of boot sections

#define HHDM_OFFSET                                                            \
  0xffff800000000000 // Higher-half direct map offset from limine.h

// Helper to convert physical to virtual address
void *p_to_v(void *p) { return (void *)((uint64_t)p + pmm_hhdm_offset); }

// Helper to convert virtual to physical address
static inline void *v_to_p(void *v) {
  return (void *)((uint64_t)v - pmm_hhdm_offset);
}

static void pmm_bitmap_set(uint64_t page_index) {
  uint8_t *bmp = p_to_v(pmm_bitmap);
  bmp[page_index / 8] |= (1 << (page_index % 8));
}

static void pmm_bitmap_unset(uint64_t page_index) {
  uint8_t *bmp = p_to_v(pmm_bitmap);
  bmp[page_index / 8] &= ~(1 << (page_index % 8));
}

static int pmm_bitmap_test(uint64_t page_index) {
  uint8_t *bmp = p_to_v(pmm_bitmap);
  return bmp[page_index / 8] & (1 << (page_index % 8));
}

void pmm_init(struct limine_memmap_response *mmap_response, uint64_t offset) {
  pmm_hhdm_offset = offset;
  uint64_t highest_addr = 0;
  for (uint64_t i = 0; i < mmap_response->entry_count; i++) {
    struct limine_memmap_entry *entry = mmap_response->entries[i];
    if (entry->base + entry->length > highest_addr) {
      highest_addr = entry->base + entry->length;
    }
  }
  pmm_total_pages = highest_addr / PAGE_SIZE;
  uint64_t bitmap_size = ALIGN_UP(pmm_total_pages / 8, PAGE_SIZE);

  // Find a large enough spot for the bitmap itself
  for (uint64_t i = 0; i < mmap_response->entry_count; i++) {
    struct limine_memmap_entry *entry = mmap_response->entries[i];
    if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
      pmm_bitmap = (uint8_t *)entry->base;
      break;
    }
  }
  if (pmm_bitmap == NULL) {
    panic("PMM: Could not find a suitable location for the memory bitmap!",
          NULL);
    return;
  }

  // Mark all memory as used initially
  memset(p_to_v(pmm_bitmap), 0xff, bitmap_size);

  // Mark usable memory as free
  for (uint64_t i = 0; i < mmap_response->entry_count; i++) {
    struct limine_memmap_entry *entry = mmap_response->entries[i];
    if (entry->type == LIMINE_MEMMAP_USABLE) {
      uint64_t pages = entry->length / PAGE_SIZE;
      pmm_usable_pages += pages;
      for (uint64_t j = 0; j < pages; j++) {
        pmm_bitmap_unset((entry->base / PAGE_SIZE) + j);
      }
    }
  }

  // Redundant kernel marking was removed as type 6 entry covers it.
  // The kernel's physical range is not type 0 (USABLE), so it remains set.

  // Mark the bitmap itself as used
  uint64_t bitmap_pages = bitmap_size / PAGE_SIZE;
  for (uint64_t i = 0; i < bitmap_pages; i++) {
    pmm_bitmap_set(((uint64_t)pmm_bitmap / PAGE_SIZE) + i);
  }

  klog(LOG_INFO, "PMM initialized.");
}

void *pmm_alloc_page() {
  for (uint64_t i = pmm_last_free_page; i < pmm_total_pages; i++) {
    if (!pmm_bitmap_test(i)) {
      pmm_bitmap_set(i);
      pmm_allocated_pages++; // Track allocation
      pmm_last_free_page = i + 1;
      return (void *)(i * PAGE_SIZE);
    }
  }

  // Wrap around
  for (uint64_t i = 0; i < pmm_last_free_page; i++) {
    if (!pmm_bitmap_test(i)) {
      pmm_bitmap_set(i);
      pmm_allocated_pages++; // Track allocation
      pmm_last_free_page = i + 1;
      return (void *)(i * PAGE_SIZE);
    }
  }

  klog(LOG_WARN, "PMM: Out of physical memory!");
  return NULL;
}

void *pmm_alloc_pages(size_t count) {
  if (count == 0)
    return NULL;
  if (count == 1)
    return pmm_alloc_page();

  for (uint64_t i = 0; i <= pmm_total_pages - count; i++) {
    int found = 1;
    for (uint64_t j = 0; j < count; j++) {
      if (pmm_bitmap_test(i + j)) {
        found = 0;
        i += j; // Skip checked pages
        break;
      }
    }

    if (found) {
      for (uint64_t j = 0; j < count; j++) {
        pmm_bitmap_set(i + j);
      }
      pmm_allocated_pages += count; // Track allocations
      return (void *)(i * PAGE_SIZE);
    }
  }

  klog(LOG_WARN, "PMM: Out of contiguous physical memory!");
  return NULL;
}

void pmm_free_page(void *p) {
  uint64_t page_index = (uint64_t)p / PAGE_SIZE;
  if (page_index < pmm_total_pages) {
    if (pmm_bitmap_test(page_index)) { // Only decrement if it was allocated
      pmm_bitmap_unset(page_index);
      pmm_allocated_pages--; // Track deallocation
      if (page_index < pmm_last_free_page) {
        pmm_last_free_page = page_index;
      }
    }
  } else {
    klog(LOG_WARN, "PMM: Attempted to free an invalid physical page.");
  }
}

uint64_t pmm_get_total_memory(void) { return pmm_usable_pages * PAGE_SIZE; }

uint64_t pmm_get_used_memory(void) { return pmm_allocated_pages * PAGE_SIZE; }
