#include "keyboard.h"
#include "event.h"
#include "isr.h"
#include "log.h"
#include "port_io.h"
#include "thread.h"
#include "signal.h"
#include "scheduler.h"
#include "fb.h"
#include <stdbool.h>
#include <string.h>

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
static bool extended_key = false;

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

char keyboard_get_char_blocking() {
    event_t ev;
    while (1) {
        event_wait(&ev);
        if (ev.type == EVENT_KEY_DOWN) {
            return (char)ev.data1;
        }
    }
}

static void push_vt100_seq(const char *s, uint8_t scancode) {
  while (*s) {
    event_t ev = {0};
    ev.type = EVENT_KEY_DOWN;
    ev.data1 = (uint64_t)(uint8_t)(*s++);
    ev.data2 = (uint64_t)scancode;
    ev.data3 = (ctrl_pressed ? 1 : 0) | (shift_pressed ? 2 : 0) | 4;
    event_push(ev);
  }
}

static void keyboard_handler(struct registers *regs) {
  (void)regs; // Suppress unused parameter warning

  uint8_t scancode = inb(0x60);
  if (scancode == 0xE0) { extended_key = true; return; }
  bool extended = extended_key;
  extended_key = false;
  event_t event = {0};

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

  // Handle extended navigation and editing keys -> Standard VT100 / ANSI escape sequences
  if (extended || (scancode >= 0x47 && scancode <= 0x53 && kbd_us[scancode] == 0)) {
    if (!(scancode & 0x80)) { // Key press
      switch (scancode) {
        case 0x48: push_vt100_seq("\033[A", scancode); return; // Up Arrow
        case 0x50: push_vt100_seq("\033[B", scancode); return; // Down Arrow
        case 0x4D: push_vt100_seq("\033[C", scancode); return; // Right Arrow
        case 0x4B: push_vt100_seq("\033[D", scancode); return; // Left Arrow
        case 0x47: push_vt100_seq("\033[H", scancode); return; // Home
        case 0x4F: push_vt100_seq("\033[F", scancode); return; // End
        case 0x49: push_vt100_seq("\033[5~", scancode); return; // Page Up
        case 0x51: push_vt100_seq("\033[6~", scancode); return; // Page Down
        case 0x52: push_vt100_seq("\033[2~", scancode); return; // Insert
        case 0x53: push_vt100_seq("\033[3~", scancode); return; // Delete
        default: break;
      }
    } else {
      return; // Ignore key release of extended keys
    }
  }

  if (scancode & 0x80) { // Key release
    event.type = EVENT_KEY_UP;
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
    if (ctrl_pressed) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 1);
        } else if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 1);
        }
    }

    // Check for Ctrl+C (character 0x03)
    if (c == 3) {
      extern thread_t *console_focus;
      extern thread_t *scheduler_find_thread(uint64_t pid);
      thread_t *target = console_focus;
      if (target && scheduler_find_thread(target->id) != target) {
        console_focus = NULL;
        target = NULL;
      }
      if (!target) target = get_current_thread();
      if (target && target->id > 1 && target->state != THREAD_DEAD) {
        bool is_shell = (strcmp(target->name, "sh") == 0 ||
                         strcmp(target->name, "/bin/sh") == 0 ||
                         strcmp(target->name, "init") == 0 ||
                         target->id <= 2);
        if (!is_shell) {
          bool swallow_event = false;
          if (target->sig_handlers[2] == SIG_IGN) {
            // Ignored by process
          } else if (target->sig_handlers[2] != SIG_DFL) {
            target->sig_pending |= (1 << 2); // Deliver custom SIGINT
            swallow_event = true;
          } else {
            // Signal/terminate foreground application safely
            target->sig_pending |= (1 << 2);
            target->exit_status = 130;
            if ((regs->cs & 3) == 3 || target != get_current_thread() ||
                target->state == THREAD_BLOCKED) {
              target->state = THREAD_DEAD;
            }
            if (target->parent_id) {
              thread_t *parent = scheduler_find_thread(target->parent_id);
              if (parent) parent->sig_pending |= (1 << 17); // SIGCHLD
            }
            if (console_focus == target) console_focus = NULL;
            fb_release_graphics_owner(target->id);
            swallow_event = true;
          }
          klog_print_str("^C\n", true);
          if (swallow_event) return;
        }
      }
    }

    event.data1 = c;
  }
  event.data3 = (ctrl_pressed ? 1 : 0) | (shift_pressed ? 2 : 0) | (extended ? 4 : 0);
  event.data2 = scancode; // Store raw scancode in data2
  event_push(event);
}

void keyboard_init() { register_irq_handler(1, keyboard_handler); }
