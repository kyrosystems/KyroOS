#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"
#include "sysnum.h" // GENERATED from abi/syscalls.def - single source of truth
// NOTE: error contract — kernel returns (uint64_t)-errno; libc wrapper sets
// errno and returns -1. Never return -1 (that is -EPERM) for other failures.

void syscall_init();
void syscall_handler(struct registers *regs);

// Registered-handler consistency check: warns if registry impl=yes numbers
// have no dispatch entry. Defined in syscall.c.
void syscall_audit_table();

#endif // SYSCALL_H
