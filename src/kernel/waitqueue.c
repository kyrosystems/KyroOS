#include "waitqueue.h"
#include "heap.h"
#include "scheduler.h"
#include <stddef.h>

void waitqueue_init(wait_queue_t *wq) {
  wq->head = NULL;
  spinlock_init(&wq->lock);
}

void waitqueue_add(wait_queue_t *wq, thread_t *thread) {
  spinlock_acquire(&wq->lock);

  wait_queue_entry_t *entry = kmalloc(sizeof(wait_queue_entry_t));
  entry->thread = thread;
  entry->next = wq->head;
  wq->head = entry;

  thread->state = THREAD_BLOCKED;

  spinlock_release(&wq->lock);
}

thread_t *waitqueue_remove_first(wait_queue_t *wq) {
  spinlock_acquire(&wq->lock);

  if (wq->head == NULL) {
    spinlock_release(&wq->lock);
    return NULL;
  }

  wait_queue_entry_t *entry = wq->head;
  wq->head = entry->next;
  thread_t *thread = entry->thread;
  kfree(entry);

  spinlock_release(&wq->lock);
  return thread;
}

void waitqueue_wakeup_one(wait_queue_t *wq) {
  thread_t *thread = waitqueue_remove_first(wq);
  if (thread) {
    thread->state = THREAD_READY;
  }
}

void waitqueue_wakeup_all(wait_queue_t *wq) {
  thread_t *thread;
  while ((thread = waitqueue_remove_first(wq)) != NULL) {
    thread->state = THREAD_READY;
  }
}

// i wanna to add waitqueue_remove_thread but idk if it's needed rn
// fuck Rust
// glory to HolyC
// i love C and HolyC
// Terry Devis i love you