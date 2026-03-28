#include "scheduler.h"
#include "heap.h"
#include "isr.h"
#include "log.h"
#include "pmm.h" 
#include "thread.h"
#include "tss.h"
#include "vmm.h"    
#include <stddef.h> 
#include "kstring.h"

static thread_t *ready_queue = NULL; 
thread_t *current_thread = NULL;

void scheduler_init() {
  ready_queue = NULL;
}

void scheduler_add_thread(thread_t *thread) {
  if (!thread) return;
  disable_interrupts();
  if (ready_queue == NULL) {
    ready_queue = thread;
    thread->next = thread; 
  } else {
    thread->next = ready_queue->next;
    ready_queue->next = thread;
    ready_queue = thread; 
  }
  enable_interrupts();
}

thread_t *get_current_thread() { return current_thread; }

void schedule() {
  disable_interrupts();
  if (!current_thread || !ready_queue) { enable_interrupts(); return; }

  thread_t *old_thread = current_thread;
  thread_t *next_thread = NULL;
  
  thread_t *curr = old_thread->next;
  do {
      if (curr->state == THREAD_READY) { next_thread = curr; break; }
      curr = curr->next;
  } while (curr != old_thread->next);

  if (!next_thread) {
      if (old_thread->state == THREAD_RUNNING || old_thread->state == THREAD_READY) {
          next_thread = old_thread;
      } else {
          curr = old_thread->next;
          do {
              if (curr->state == THREAD_READY || curr->state == THREAD_RUNNING) {
                  next_thread = curr; break;
              }
              curr = curr->next;
          } while (curr != old_thread->next);
          if (!next_thread) panic("Scheduler: No runnable threads!", NULL);
      }
  }

  if (old_thread != next_thread) {
    if (old_thread->state == THREAD_RUNNING) old_thread->state = THREAD_READY;
    if (next_thread->state == THREAD_READY) next_thread->state = THREAD_RUNNING;
    current_thread = next_thread;
    if (next_thread->stack) tss_set_stack((uint64_t)next_thread->stack + KERNEL_STACK_SIZE);
    thread_switch(old_thread, next_thread);
  }
  enable_interrupts();
}

void scheduler_print_threads() {
    disable_interrupts();
    if (!ready_queue) {
        klog_print_str("No threads in ready queue.\n", true);
        enable_interrupts();
        return;
    }

    klog_print_str("PID\tState\n", true);
    klog_print_str("---\t-----\n", true);

    thread_t *curr = ready_queue;
    do {
        char buf[64];
        char *state_str = "UNKNOWN";
        switch(curr->state) {
            case THREAD_RUNNING: state_str = "RUNNING"; break;
            case THREAD_READY:   state_str = "READY"; break;
            case THREAD_BLOCKED: state_str = "BLOCKED"; break;
            case THREAD_DEAD:    state_str = "DEAD"; break;
        }
        
        ksprintf(buf, "%d\t%s\n", (int)curr->id, state_str);
        klog_print_str(buf, true);

        curr = curr->next;
    } while (curr != ready_queue);
    
    enable_interrupts();
}
