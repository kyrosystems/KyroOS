#include "keyboard.h"
#include "event.h"
#include "isr.h"
#include "log.h" // Added
#include "port_io.h"
#include <stdbool.h>

// Basic US QWERTY scancode to ASCII map

static const unsigned char kbd_us[128] = {
    0,    27,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    '\n', 0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    0,    0,   0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,    0,
    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,
};

static const unsigned char kbd_us_shift[128] = {
    0,    0,    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',
    '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{',  '}',
    '\n', 0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"',  '~',
    0,    '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,    '*',
    0,    ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    0,    0,   0,   0,   0,   0,   '_', 0,   0,   0,   '+', 0,    0,
    0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,
};

static bool ctrl_pressed = false;
static bool shift_pressed = false;
static bool caps_lock_on = false;

bool keyboard_is_ctrl_pressed() {
    return ctrl_pressed;
}

bool keyboard_is_shift_pressed() {
    return shift_pressed;
}

char keyboard_get_char() {
  event_t ev;

  while (event_pop(&ev)) {
    if (ev.type == EVENT_KEY_DOWN) {
      return (char)ev.data1;
    }
  }
  return 0;
}

static void keyboard_handler(struct registers *regs) {
  (void)regs; // Suppress unused parameter warning

  uint8_t scancode = inb(0x60);
  event_t event;

  // Handle modifier keys
  if (scancode == 0x1D) { // Left Ctrl pressed
    ctrl_pressed = true;
    return;
  } else if (scancode == 0x9D) { // Left Ctrl released
    ctrl_pressed = false;
    return;
  } else if (scancode == 0x2A || scancode == 0x36) { // Left or Right Shift pressed
    shift_pressed = true;
    return;
  } else if (scancode == 0xAA || scancode == 0xB6) { // Left or Right Shift released
    shift_pressed = false;
    return;
  } else if (scancode == 0x3A) { // Caps Lock pressed
    caps_lock_on = !caps_lock_on;
    return;
  }

  if (scancode & 0x80) { // Key release
    event.type = EVENT_KEY_UP;
    // For simplicity, we don't apply shift/caps on release, just send the base character
    event.data1 = kbd_us[scancode & 0x7F];
  } else { // Key press
    event.type = EVENT_KEY_DOWN;
    char c = kbd_us[scancode];
    if (c >= 'a' && c <= 'z') {
        if (shift_pressed ^ caps_lock_on) {
            c = kbd_us_shift[scancode];
        }
    } else {
        if (shift_pressed) {
            c = kbd_us_shift[scancode];
        }
    }
    event.data1 = c;
  }
  event.data2 = scancode; // Store raw scancode in data2
  event_push(event);
}

void keyboard_init() { register_irq_handler(1, keyboard_handler); }