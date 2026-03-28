#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "thread.h"

void scheduler_init();
void schedule();
void scheduler_add_thread(thread_t *thread);
thread_t *get_current_thread();
uint64_t timer_get_ticks(); // From isr.h/timer.c but declared here for convenience? No, keep in isr.h or timer.h

void scheduler_print_threads(); // New function for 'ps' command

#endif // SCHEDULER_H
