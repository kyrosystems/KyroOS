#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "thread.h"

void scheduler_init();
void schedule();
void thread_sleep(uint64_t ticks);
void scheduler_add_thread(thread_t *thread);
thread_t *get_current_thread();
uint64_t timer_get_ticks(); // From isr.h/timer.c but declared here for convenience? No, keep in isr.h or timer.h

void scheduler_print_threads(); // New function for 'ps' command

// Keyboard-event focus: while a userspace program runs, the shell must not
// consume console events (it would steal e.g. 'q' from sysmon). Set by
// thread_create_userspace, cleared by the scheduler reaper on exit.
extern thread_t *console_focus;
int scheduler_waitpid(uint64_t target_pid, int *status_out, int options);

#endif // SCHEDULER_H
