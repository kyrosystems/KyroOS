#include "spinlock.h"

void spinlock_init(spinlock_t *lock) { lock->locked = 0; }

void spinlock_acquire(spinlock_t *lock) {
  // Use GCC atomic builtin for test-and-set
  while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
    // Pause instruction to reduce CPU usage while spinning
    __asm__ volatile("pause");
  }
}

void spinlock_release(spinlock_t *lock) {
  __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}

int spinlock_try_acquire(spinlock_t *lock) {
  // Returns 1 if lock acquired, 0 if already locked
  return !__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE);
}
