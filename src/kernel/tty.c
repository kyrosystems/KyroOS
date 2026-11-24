#include "tty.h"
#include "event.h"
#include "fb.h" // Include framebuffer header
#include "heap.h"
#include "keyboard.h"
#include "kstring.h"
#include "log.h" // For klog_putchar, klog_print_str
#include "vfs.h"
#include "thread.h"
#include "signal.h"
#include <stdint.h>
#include <string.h>

// console_x, console_y are extern from log.h
// console_cols, console_rows, char_width, char_height are also implicitly managed by klog_putchar
// Only foreground/background colors remain local state for this TTY instance if needed,
// but for now, we'll let log.c manage colors.

uint32_t tty_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                  uint8_t *buffer) {
  (void)node;
  (void)offset;

  if (size == 0) {
    return 0;
  }

  event_t ev;
  while (1) {
    if (event_pop(&ev)) {
      if (ev.type == EVENT_KEY_DOWN) {
        char c = (char)ev.data1;
        if (c == 3) { // Ctrl+C: Send SIGINT
          extern thread_t *console_focus;
          extern thread_t *scheduler_find_thread(uint64_t pid);
          thread_t *target = console_focus ? console_focus : get_current_thread();
          if (target && target->id > 1) {
            bool is_shell = (strcmp(target->name, "sh") == 0 ||
                             strcmp(target->name, "/bin/sh") == 0 ||
                             strcmp(target->name, "init") == 0 ||
                             target->id <= 2);
            if (!is_shell) {
              if (target->sig_handlers[2] == SIG_IGN) {
                // Ignored
              } else if (target->sig_handlers[2] != SIG_DFL) {
                target->sig_pending |= (1 << 2);
              } else {
                target->exit_status = 130;
                target->state = THREAD_DEAD;
                if (target->parent_id) {
                  thread_t *parent = scheduler_find_thread(target->parent_id);
                  if (parent) parent->sig_pending |= (1 << 17);
                }
                if (console_focus == target) console_focus = NULL;
                fb_release_graphics_owner(target->id);
              }
              klog_print_str("^C\n", true);
            }
          }
          buffer[0] = c;
          return 1;
        }
        if (c) {
          buffer[0] = c;
          return 1;
        }
      }
    }
    __asm__ __volatile__("hlt");
  }
}

uint32_t tty_write(vfs_node_t *node, uint64_t offset, uint32_t size,
                   uint8_t *buffer) {
  (void)node;
  (void)offset;

  // klog_print_str handles both serial and framebuffer output, and cursor management
  // It also calls fb_flush internally as part of its framebuffer update.
  klog_print_str((const char *)buffer, true); // Flush immediately after writing to tty
  
  return size;
}

void tty_init() {
  vfs_node_t *tty = vfs_resolve_path(vfs_root, "/dev/tty");
  if (tty) {
    tty->read = tty_read;
    tty->write = tty_write;
    klog(LOG_INFO, "TTY: /dev/tty handlers registered.");
  } else {
    klog(LOG_ERROR, "TTY: Failed to resolve /dev/tty. This should have been created by KyroFS.");
  }
}