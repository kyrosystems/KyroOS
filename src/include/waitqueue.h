#ifndef WAITQUEUE_H
#define WAITQUEUE_H

#include "spinlock.h"
#include "thread.h"


typedef struct wait_queue_entry {
  thread_t *thread;
  struct wait_queue_entry *next;
} wait_queue_entry_t;

typedef struct {
  wait_queue_entry_t *head;
  spinlock_t lock;
} wait_queue_t;

void waitqueue_init(wait_queue_t *wq);
void waitqueue_add(wait_queue_t *wq, thread_t *thread);
thread_t *waitqueue_remove_first(wait_queue_t *wq);
void waitqueue_wakeup_one(wait_queue_t *wq);
void waitqueue_wakeup_all(wait_queue_t *wq);

#endif // WAITQUEUE_H
