#include "scheduler.h"
#include "heap.h"
#include "isr.h"
#include "log.h"
#include "pmm.h" // For pmm_free_page
#include "thread.h"
#include "tss.h"
#include "vmm.h"    // For vmm_unmap_page, vmm_destroy_address_space, PAGE_SIZE
#include <stddef.h> // for NULL

// Simple round-robin scheduler

static thread_t *ready_queue = NULL; // Head of the ready queue
thread_t *current_thread = NULL;

void scheduler_init() {
  klog(LOG_INFO, "Scheduler: Initializing...");
  // The main kernel thread is already running, no need to add it to the ready
  // queue. `current_thread` is initialized in thread_init()
  ready_queue = NULL;
  klog(LOG_INFO, "Scheduler initialized.");
}

void scheduler_add_thread(thread_t *thread) {
  if (!thread)
    return;

  disable_interrupts();
  if (ready_queue == NULL) {
    ready_queue = thread;
    thread->next = thread; // Circular list
  } else {
    thread->next = ready_queue->next;
    ready_queue->next = thread;
    ready_queue = thread; // New thread becomes the tail
  }
  enable_interrupts();
}

thread_t *get_current_thread() { return current_thread; }

// The core scheduler function
void schedule() {
  disable_interrupts();

  if (!current_thread || !ready_queue) {
    enable_interrupts();
    return;
  }

  thread_t *old_thread = current_thread;
  thread_t *next_thread =
      ready_queue->next; // Start looking from the next thread in the queue

  // 1. Clean up DEAD threads and find the next READY thread
  thread_t *prev = ready_queue;
  thread_t *start_node = next_thread;
  thread_t *curr = start_node;

  do {
    if (curr->state == THREAD_DEAD &&
        curr->id != 0) { // Don't kill kernel thread
      if (curr == old_thread) {
        // Can't free the current thread yet, it needs to switch away first.
        // It will be freed by the NEXT scheduler call.
        prev = curr;
        curr = curr->next;
        continue;
      }
      thread_t *dead_thread = curr;
      prev->next = curr->next;
      curr = curr->next;

      if (ready_queue == dead_thread) {
        ready_queue = prev;
      }

      // Cleanup logic (simplified for brevity, keeping existing logic)
      if (dead_thread->user_stack_base) {
        for (uint64_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
          void *vaddr = (void *)((uint64_t)dead_thread->user_stack_base + i);
          void *phys_addr = vmm_unmap_page(dead_thread->pml4, vaddr);
          if (phys_addr)
            pmm_free_page(phys_addr);
        }
      }
      if (dead_thread->pml4 && dead_thread->id != 0) {
        vmm_destroy_address_space(dead_thread->pml4);
      }
      if (dead_thread->stack)
        kfree(dead_thread->stack);
      kfree(dead_thread);

      if (ready_queue == NULL)
        break; // All threads gone? (Shouldn't happen with idle/kernel)
      continue;
    }

    if (curr->state == THREAD_READY) {
      next_thread = curr;
      break;
    }

    prev = curr;
    curr = curr->next;
  } while (curr != start_node);

  // 2. Decide if we switch
  if (next_thread->state != THREAD_READY) {
    // If we only have the current thread and it's still running, keep it.
    if (old_thread->state == THREAD_RUNNING) {
      enable_interrupts();
      return;
    }
    // If current is blocked and nothing else is ready, we are in trouble.
    panic("Scheduler: No runnable threads!", NULL);
  }

  // 3. Perform Switch
  ready_queue = next_thread; // Tail of the queue for next time

  if (old_thread->state == THREAD_RUNNING) {
    old_thread->state = THREAD_READY;
  }
  next_thread->state = THREAD_RUNNING;
  current_thread = next_thread;

  if (old_thread != next_thread) {
    // Update TSS and switch
    if (next_thread->stack) {
      tss_set_stack((uint64_t)next_thread->stack + KERNEL_STACK_SIZE);
    }
    thread_switch(old_thread, next_thread);
  }

  enable_interrupts();
}
