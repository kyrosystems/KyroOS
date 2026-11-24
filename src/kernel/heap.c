#include "heap.h"
#include "log.h"
#include "pmm.h"
#include "spinlock.h"
#include "vmm.h"
#include <stddef.h>
#include <stdint.h>

// Simple linked list based heap allocator

typedef struct header {
  struct header *next;
  size_t size; // Size of the block in units of header size
} header_t;

static spinlock_t g_heap_lock;
static header_t base; // Empty list to start
static header_t *freelist = NULL;

static void _kfree_unlocked(void *ap) {
  if (ap == NULL) {
    return;
  }

  header_t *bp = (header_t *)ap - 1; // Point to block header
  header_t *p;

  if (!freelist) { // Should not happen, implies heap corruption or uninitialized
    klog(LOG_ERROR, "Heap: kfree called with uninitialized freelist or corrupt heap!");
    return;
  }

  for (p = freelist; !(bp > p && bp < p->next); p = p->next) {
    if (p >= p->next && (bp > p || bp < p->next)) {
      break; // Freed block at start or end of arena
    }
  }

  if (bp + bp->size == p->next) { // Coalesce with upper neighbor
    bp->size += p->next->size;
    bp->next = p->next->next;
  } else {
    bp->next = p->next;
  }

  if (p + p->size == bp) { // Coalesce with lower neighbor
    p->size += bp->size;
    p->next = bp->next;
  } else {
    p->next = bp;
  }
  freelist = p;
}

// Ask the OS for more memory (caller holds g_heap_lock)
static header_t *morecore(size_t nunits) {
  if (nunits < 1024) {
    nunits = 1024; // Minimum request is 1024 * sizeof(header_t)
  }

  uint64_t nbytes = nunits * sizeof(header_t);
  uint64_t npages = (nbytes + PAGE_SIZE - 1) / PAGE_SIZE;
  void *page = pmm_alloc_pages(npages);
  if (page == NULL) {
    klog(LOG_WARN, "Heap: pmm_alloc_pages failed in morecore.");
    return NULL;
  }

  // Wrap physical pages in HHDM virtual address
  header_t *up = (header_t *)p_to_v(page);
  up->size = npages * PAGE_SIZE / sizeof(header_t);
  _kfree_unlocked((void *)(up + 1));
  return freelist;
}

void heap_init() {
  spinlock_init(&g_heap_lock);
  base.next = &base;
  base.size = 0;
  freelist = &base;
  serial_print("Heap: freelist setup in heap_init().\n"); // Use serial_print here to avoid kmalloc before it's ready
}

void *kmalloc(size_t nbytes) {
  if (nbytes == 0) {
    return NULL;
  }

  uint64_t iflags = spinlock_acquire_irqsave(&g_heap_lock);

  // Calculate number of header-sized units required
  size_t nunits = (nbytes + sizeof(header_t) - 1) / sizeof(header_t) + 1;

  header_t *prevp = freelist;
  header_t *p;

  if (!freelist) { // Should not happen after heap_init
    spinlock_release_irqrestore(&g_heap_lock, iflags);
    klog(LOG_ERROR, "Heap: kmalloc called with uninitialized freelist!");
    return NULL;
  }

  for (p = prevp->next;; prevp = p, p = p->next) {
    if (p->size >= nunits) {   // Big enough
      if (p->size == nunits) { // Exactly
        prevp->next = p->next;
      } else { // Allocate tail end
        p->size -= nunits;
        p += p->size;
        p->size = nunits;
      }
      freelist = prevp;
      spinlock_release_irqrestore(&g_heap_lock, iflags);
      return (void *)(p + 1);
    }
    if (p == freelist) { // Wrapped around free list, need more memory
      if ((p = morecore(nunits)) == NULL) {
        spinlock_release_irqrestore(&g_heap_lock, iflags);
        klog(LOG_WARN, "Heap: out of memory from morecore!");
        return NULL; // No memory left
      }
    }
  }
}

void kfree(void *ap) {
  if (ap == NULL) {
    return;
  }

  uint64_t iflags = spinlock_acquire_irqsave(&g_heap_lock);
  _kfree_unlocked(ap);
  spinlock_release_irqrestore(&g_heap_lock, iflags);
}

void *krealloc(void *ptr, size_t new_size) {
  if (ptr == NULL) {
    return kmalloc(new_size);
  }
  if (new_size == 0) {
    kfree(ptr);
    return NULL;
  }

  header_t *bp = (header_t *)ptr - 1;
  size_t old_size = (bp->size - 1) * sizeof(header_t);

  if (old_size >= new_size) {
    return ptr; // It's big enough already
  }

  void *new_ptr = kmalloc(new_size);
  if (new_ptr == NULL) {
    klog(LOG_WARN, "Heap: krealloc: kmalloc for new_ptr failed.");
    return NULL;
  }

  // Naive copy
  uint8_t *src = (uint8_t *)ptr;
  uint8_t *dst = (uint8_t *)new_ptr;
  for (size_t i = 0; i < old_size; i++) {
    dst[i] = src[i];
  }

  kfree(ptr);
  return new_ptr;
}