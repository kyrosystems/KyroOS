#ifndef MOUSE_H
#define MOUSE_H

#include "isr.h" // For struct registers
#include <stdint.h>

void mouse_init();
void mouse_handler(struct registers *regs);
void mouse_get_position(int32_t *x, int32_t *y);

#endif // MOUSE_H