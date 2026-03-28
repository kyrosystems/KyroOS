#include "tty.h"
#include "event.h"
#include "fb.h" // Include framebuffer header
#include "heap.h"
#include "keyboard.h"
#include "kstring.h"
#include "log.h" // For klog_putchar, klog_print_str
#include "vfs.h"
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