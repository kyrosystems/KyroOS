#include "tty.h"
#include "event.h"
#include "fb.h" // Include framebuffer header
#include "heap.h"
#include "keyboard.h"
#include "kstring.h"
#include "log.h" // For klog_putchar, klog_print_str
#include "scheduler.h"
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

  if (!buffer || size == 0) {
    return 0;
  }

  event_t ev;
  while (1) {
    thread_t *cur = get_current_thread();
    if (cur && (cur->state == THREAD_DEAD || (cur->sig_pending & (1 << 2)))) {
      if (cur->state != THREAD_DEAD && cur->sig_handlers[2] == SIG_DFL) {
        cur->exit_status = 130;
        if (console_focus == cur) console_focus = NULL;
        fb_release_graphics_owner(cur->id);
        thread_exit();
      }
      return 0;
    }

    if (console_focus && console_focus != cur) {
      thread_sleep(1);
      continue;
    }

    if (event_pop(&ev)) {
      if (ev.type == EVENT_KEY_DOWN) {
        char c = (char)ev.data1;
        if (c == 3) { // Ctrl+C: Send SIGINT
          extern thread_t *scheduler_find_thread(uint64_t pid);
          thread_t *target = console_focus;
          if (target && scheduler_find_thread(target->id) != target) {
            console_focus = NULL;
            target = NULL;
          }
          if (!target) target = cur;
          if (target && target->id > 1 && target->state != THREAD_DEAD) {
            bool is_shell = (strcmp(target->name, "sh") == 0 ||
                             strcmp(target->name, "/bin/sh") == 0 ||
                             strcmp(target->name, "init") == 0 ||
                             target->id <= 2);
            if (!is_shell) {
              if (target->sig_handlers[2] == SIG_IGN) {
                // Ignored
              } else if (target->sig_handlers[2] != SIG_DFL) {
                target->sig_pending |= (1 << 2);
                klog_print_str("^C\n", true);
                if (target == cur) return 0;
                continue;
              } else {
                klog_print_str("^C\n", true);
                if (target == cur) {
                  cur->exit_status = 130;
                  if (console_focus == cur) console_focus = NULL;
                  fb_release_graphics_owner(cur->id);
                  thread_exit();
                }
                target->sig_pending |= (1 << 2);
                target->exit_status = 130;
                target->state = THREAD_DEAD;
                if (target->parent_id) {
                  thread_t *parent = scheduler_find_thread(target->parent_id);
                  if (parent) parent->sig_pending |= (1 << 17);
                }
                if (console_focus == target) console_focus = NULL;
                fb_release_graphics_owner(target->id);
                continue;
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
    thread_sleep(1);
  }
}

uint32_t tty_write(vfs_node_t *node, uint64_t offset, uint32_t size,
                   uint8_t *buffer) {
  (void)node;
  (void)offset;
  if (!buffer || size == 0) return 0;

  // klog_write safely writes exact buffer length without assuming null-termination
  klog_write((const char *)buffer, size, true);
  
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