#include "mutex.h"
#include "scheduler.h"
#include <stddef.h>


void mutex_init(mutex_t *mutex) {
  spinlock_init(&mutex->lock);
  waitqueue_init(&mutex->wait_queue);
  mutex->locked = 0;
  mutex->owner = NULL;
}

void mutex_lock(mutex_t *mutex) {
  spinlock_acquire(&mutex->lock);

  if (mutex->locked) {
    // Mutex is already locked, add current thread to wait queue
    thread_t *current = get_current_thread();
    waitqueue_add(&mutex->wait_queue, current);
    spinlock_release(&mutex->lock);

    // Yield to scheduler - thread is now BLOCKED
    schedule();
  } else {
    // Mutex is free, acquire it
    mutex->locked = 1;
    mutex->owner = get_current_thread();
    spinlock_release(&mutex->lock);
  }
}

void mutex_unlock(mutex_t *mutex) {
  spinlock_acquire(&mutex->lock);

  if (mutex->owner != get_current_thread()) {
    spinlock_release(&mutex->lock);
    return; // Only owner can unlock
  }

  mutex->locked = 0;
  mutex->owner = NULL;

  // Wake up one waiting thread
  waitqueue_wakeup_one(&mutex->wait_queue);

  spinlock_release(&mutex->lock);
}

int mutex_try_lock(mutex_t *mutex) {
  spinlock_acquire(&mutex->lock);

  if (mutex->locked) {
    spinlock_release(&mutex->lock);
    return 0; // Failed to acquire
  }

  mutex->locked = 1;
  mutex->owner = get_current_thread();
  spinlock_release(&mutex->lock);
  return 1; // Successfully acquired
}
