#ifndef PANIC_H
#define PANIC_H

#include <stddef.h> // For size_t
#include <stdint.h>

// Define an enum for known panic reasons
typedef enum {
    PANIC_GENERIC = 0,
    PANIC_FB_INIT_FAIL,
    PANIC_NO_RUNNABLE_THREADS,
    PANIC_KPM_MAIN_THREAD_ALLOC_FAIL, // Used to be Failed to Allocate Main Kernel Thread
    PANIC_RETURNED_TO_DEAD_THREAD,
    PANIC_KMALLOC_FAIL,
    // Add more as needed
} panic_reason_t;

// Function to trigger a kernel panic
void trigger_kernel_panic(panic_reason_t reason);

#endif // PANIC_H