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
#include "socket.h"

static thread_t *ready_queue = NULL;
thread_t *console_focus = NULL; // keyboard focus owner (see scheduler.h) 
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

// Reap THREAD_DEAD threads: free kernel stack, fd table (sockets) and the
// user address space. Without this every exited process leaked its whole
// address space (~MBs per command). Called from schedule() after a context
// switch, in the resumed thread's context, with interrupts disabled.
static void scheduler_reap_dead(thread_t *guard) {
  if (!ready_queue) return;
  int count = 0;
  thread_t *c = ready_queue;
  do { count++; c = c->next; } while (c && c != ready_queue && count < 1024);
  if (count >= 1024) return; // ring corrupt: refuse to walk 
  thread_t *prev = ready_queue;
  thread_t *cur = ready_queue->next;
  for (int i = 0; i < count && cur && ready_queue; i++) {
    thread_t *next = cur->next;
    if (cur->state == THREAD_DEAD && cur != guard) {
      for (int f = 0; f < MAX_FILES; f++) {
        if (cur->fd_table[f].type == FD_TYPE_SOCKET && cur->fd_table[f].data.sock) {
          sock_close(cur->fd_table[f].data.sock);
          cur->fd_table[f].type = FD_TYPE_NONE;
        }
      }
      // destroy the user address space unless it is the active CR3 (kernel threads share the kernel pml4 and must never free it).

      if (cur->pml4 && cur->pml4 != vmm_get_current_pml4()) {
        vmm_destroy_address_space(cur->pml4);
      }
      if (cur->stack) kfree(cur->stack);
      prev->next = cur->next;
      if (ready_queue == cur) {
        ready_queue = (cur->next == cur) ? NULL : cur->next;
      }
      if (console_focus == cur) console_focus = NULL;
      kfree(cur);
      klog(LOG_DEBUG, "REAPER: reaped dead thread (address space freed)");
    } else {
      prev = cur;
    }
    cur = next;
  }
}

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
    scheduler_reap_dead(get_current_thread());
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
