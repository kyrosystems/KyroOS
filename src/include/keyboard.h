#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>

bool keyboard_is_ctrl_pressed();
bool keyboard_is_shift_pressed();

void keyboard_init();

#endif // KEYBOARD_H
