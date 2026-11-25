#define KYRO_KERNEL_DISPLAY
#include "display.h"
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

#include "pipe.h"
#include "fb.h"
#include "spinlock.h"
#include <stdbool.h>

static spinlock_t sched_lock;
static thread_t *ready_queue = NULL;
thread_t *console_focus = NULL; // keyboard focus owner (see scheduler.h) 
thread_t *current_thread = NULL;

typedef struct {
  uint64_t pid;
  uint64_t parent_id;
  int status;
  int active;
} dead_child_record_t;

#define MAX_DEAD_RECORDS 64
static dead_child_record_t dead_records[MAX_DEAD_RECORDS];

void scheduler_init() {
  spinlock_init(&sched_lock);
  ready_queue = NULL;
  memset(dead_records, 0, sizeof(dead_records));
}

void scheduler_add_thread(thread_t *thread) {
  if (!thread) return;
  uint64_t iflags = spinlock_acquire_irqsave(&sched_lock);
  if (ready_queue == NULL) {
    ready_queue = thread;
    thread->next = thread; 
  } else {
    thread->next = ready_queue->next;
    ready_queue->next = thread;
    ready_queue = thread; 
  }
  spinlock_release_irqrestore(&sched_lock, iflags);
}

thread_t *get_current_thread() { return current_thread; }

static bool scheduler_pid_alive_locked(uint64_t pid) {
  if (!ready_queue) return false;
  thread_t *c = ready_queue;
  do {
    if (c->id == pid && c->state != THREAD_DEAD) return true;
    c = c->next;
  } while (c && c != ready_queue);
  return false;
}

thread_t *scheduler_find_thread(uint64_t pid) {
  uint64_t iflags = spinlock_acquire_irqsave(&sched_lock);
  thread_t *found = NULL;
  if (ready_queue) {
    thread_t *c = ready_queue;
    do {
      if (c->id == pid && c->state != THREAD_DEAD) {
        found = c;
        break;
      }
      c = c->next;
    } while (c && c != ready_queue);
  }
  spinlock_release_irqrestore(&sched_lock, iflags);
  return found;
}

// Reap THREAD_DEAD threads: free kernel stack, fd table (sockets/pipes) and the
// user address space. Without this every exited process leaked its whole
// address space (~MBs per command). Called from schedule() after a context
// switch, in the resumed thread's context, with interrupts disabled.
static void scheduler_reap_dead(thread_t *guard) {
  thread_t *dead_list = NULL;

  uint64_t iflags = spinlock_acquire_irqsave(&sched_lock);
  if (ready_queue) {
    int count = 0;
    thread_t *c = ready_queue;
    do { count++; c = c->next; } while (c && c != ready_queue && count < 1024);
    if (count < 1024) {
      thread_t *prev = ready_queue;
      thread_t *cur = ready_queue->next;
      for (int i = 0; i < count && cur && ready_queue; i++) {
        thread_t *next = cur->next;
        if (cur->state == THREAD_DEAD && cur != guard) {
          // Unlink cur from ready_queue
          if (cur->next == cur) {
            ready_queue = NULL;
          } else {
            prev->next = cur->next;
            if (ready_queue == cur) {
              ready_queue = prev;
            }
          }
          if (console_focus == cur) console_focus = NULL;

          // Discard any orphaned dead records belonging to this dying parent
          for (int r = 0; r < MAX_DEAD_RECORDS; r++) {
            if (dead_records[r].active && dead_records[r].parent_id == cur->id) {
              dead_records[r].active = 0;
            }
          }

          // Only record child termination if parent is still alive to waitpid on it
          if (scheduler_pid_alive_locked(cur->parent_id)) {
            int slot = -1;
            for (int r = 0; r < MAX_DEAD_RECORDS; r++) {
              if (!dead_records[r].active) {
                slot = r;
                break;
              }
            }
            if (slot < 0) {
              // Reclaim stale records whose parents are no longer alive
              for (int r = 0; r < MAX_DEAD_RECORDS; r++) {
                if (dead_records[r].active &&
                    !scheduler_pid_alive_locked(dead_records[r].parent_id)) {
                  dead_records[r].active = 0;
                  if (slot < 0) slot = r;
                }
              }
            }
            if (slot >= 0) {
              dead_records[slot].pid = cur->id;
              dead_records[slot].parent_id = cur->parent_id;
              dead_records[slot].status = cur->exit_status;
              dead_records[slot].active = 1;
            }
          }

          cur->next = dead_list;
          dead_list = cur;
        } else {
          prev = cur;
        }
        cur = next;
      }
    }
  }
  spinlock_release_irqrestore(&sched_lock, iflags);

  while (dead_list) {
    thread_t *cur = dead_list;
    dead_list = cur->next;
    cur->next = NULL;

    for (int f = 0; f < MAX_FILES; f++) fd_release(&cur->fd_table[f]);

    // Destroy user address space; switch away first if CR3 still points to it
    if (cur->pml4 && cur->pml4 != kernel_pml4) {
      if (cur->pml4 == vmm_get_current_pml4()) {
        vmm_switch_address_space(kernel_pml4);
      }
      vmm_destroy_address_space(cur->pml4);
    }
    display_release(cur);
    fb_release_graphics_owner(cur->id);
    if (cur->stack) kfree(cur->stack);
    kfree(cur);
    klog(LOG_DEBUG, "REAPER: reaped dead thread (address space freed)");
  }
}

int scheduler_waitpid(uint64_t target_pid, int *status_out, int options) {
  thread_t *cur = get_current_thread();
  if (!cur) return -1;

  while (1) {
    uint64_t iflags = spinlock_acquire_irqsave(&sched_lock);
    // 1. Check if any dead record matches
    for (int r = 0; r < MAX_DEAD_RECORDS; r++) {
      if (dead_records[r].active) {
        bool match = false;
        if (target_pid == (uint64_t)-1 || target_pid == 0) {
          match = (dead_records[r].parent_id == cur->id);
        } else {
          match = (dead_records[r].pid == target_pid && dead_records[r].parent_id == cur->id);
        }
        if (match) {
          uint64_t reaped_pid = dead_records[r].pid;
          int status = (dead_records[r].status & 0xFF) << 8;
          dead_records[r].active = 0;
          spinlock_release_irqrestore(&sched_lock, iflags);
          if (status_out) {
            *status_out = status;
          }
          return (int)reaped_pid;
        }
      }
    }

    // 2. Check if matching child is still in ready_queue
    bool child_found = false;
    if (ready_queue) {
      thread_t *t = ready_queue;
      do {
        if ((target_pid == (uint64_t)-1 || target_pid == 0) ? (t->parent_id == cur->id) : (t->id == target_pid && t->parent_id == cur->id)) {
          child_found = true;
          break;
        }
        t = t->next;
      } while (t && t != ready_queue);
    }

    if (!child_found) {
      spinlock_release_irqrestore(&sched_lock, iflags);
      return -10; // -ECHILD
    }

    // Child is running. If WNOHANG (options & 1), return 0 immediately.
    if (options & 1) {
      spinlock_release_irqrestore(&sched_lock, iflags);
      return 0;
    }

    // Yield and wait for child to terminate
    spinlock_release_irqrestore(&sched_lock, iflags);
    schedule();
  }
}

void thread_sleep(uint64_t ticks) {
  if (ticks == 0) {
    schedule();
    return;
  }
  uint64_t iflags = save_interrupts_and_disable();
  spinlock_acquire(&sched_lock);
  thread_t *cur = current_thread;
  if (cur) {
    cur->wake_ticks = timer_get_ticks() + ticks;
    cur->state = THREAD_BLOCKED;
  }
  spinlock_release(&sched_lock);
  restore_interrupts(iflags);
  schedule();
}

void schedule() {
  uint64_t iflags = save_interrupts_and_disable();
  spinlock_acquire(&sched_lock);
  if (!current_thread || !ready_queue) {
    spinlock_release(&sched_lock);
    restore_interrupts(iflags);
    return;
  }

  // Wake any sleeping threads whose timer has elapsed
  uint64_t now_ticks = timer_get_ticks();
  thread_t *t = ready_queue;
  do {
    if (t->state == THREAD_BLOCKED && t->wake_ticks > 0 && now_ticks >= t->wake_ticks) {
      t->state = THREAD_READY;
      t->wake_ticks = 0;
    }
    t = t->next;
  } while (t && t != ready_queue);

  thread_t *old_thread = current_thread;
  thread_t *next_thread = NULL;
  thread_t *idle_cand = NULL;
  
  thread_t *curr = old_thread->next;
  do {
      if (curr->state == THREAD_READY) {
          if (strcmp(curr->name, "idle") == 0) {
              if (!idle_cand) idle_cand = curr;
          } else {
              next_thread = curr;
              break;
          }
      }
      curr = curr->next;
  } while (curr != old_thread->next);

  if (!next_thread && idle_cand) {
      next_thread = idle_cand;
  }

  if (!next_thread) {
      if ((old_thread->state == THREAD_RUNNING || old_thread->state == THREAD_READY) && strcmp(old_thread->name, "idle") != 0) {
          next_thread = old_thread;
      } else {
          curr = old_thread->next;
          do {
              if (curr->state == THREAD_READY || curr->state == THREAD_RUNNING) {
                  next_thread = curr; break;
              }
              curr = curr->next;
          } while (curr != old_thread->next);
          if (!next_thread) {
              if (old_thread->state == THREAD_RUNNING || old_thread->state == THREAD_READY) {
                  next_thread = old_thread;
              } else {
                  panic("Scheduler: No runnable threads!", NULL);
              }
          }
      }
  }

  if (old_thread != next_thread) {
    if (old_thread->state == THREAD_RUNNING) old_thread->state = THREAD_READY;
    if (next_thread->state == THREAD_READY) next_thread->state = THREAD_RUNNING;
    current_thread = next_thread;
    if (next_thread->stack) tss_set_stack((uint64_t)next_thread->stack + KERNEL_STACK_SIZE);
    // Userspace uses SSE even though the kernel is built without it
    __asm__ volatile("fxsave64 %0" : "=m"(old_thread->fpu_state));
    __asm__ volatile("fxrstor64 %0" : : "m"(next_thread->fpu_state));

    spinlock_release(&sched_lock);
    thread_switch(old_thread, next_thread);
    scheduler_reap_dead(get_current_thread());
    restore_interrupts(iflags);
  } else {
    spinlock_release(&sched_lock);
    restore_interrupts(iflags);
  }
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
