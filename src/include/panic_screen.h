#ifndef PANIC_SCREEN_H
#define PANIC_SCREEN_H

#include "isr.h"
#include "panic.h" // Include panic.h for panic_reason_t
#include <stdint.h>
#include <stddef.h> // For size_t

void panic_screen_init(void);
void panic_screen_show(const char *message, struct registers *regs);

#endif // PANIC_SCREEN_H