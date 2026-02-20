#ifndef MUTEX_H
#define MUTEX_H

#include "spinlock.h"
#include "waitqueue.h"

typedef struct {
  spinlock_t lock;
  wait_queue_t wait_queue;
  int locked;
  thread_t *owner;
} mutex_t;

void mutex_init(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
int mutex_try_lock(mutex_t *mutex);

#endif // MUTEX_H
