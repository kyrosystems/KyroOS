#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>

bool keyboard_is_ctrl_pressed();
bool keyboard_is_shift_pressed();

void keyboard_init();
char keyboard_get_char();
char keyboard_get_char_blocking();

#endif // KEYBOARD_H
