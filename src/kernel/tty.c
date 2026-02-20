#include "tty.h"
#include "event.h"
#include "heap.h"
#include "keyboard.h"
#include "kstring.h"
#include "log.h"
#include "vfs.h"
#include <stdint.h>
#include <string.h>

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

  char kbuf[size + 1];
  memcpy(kbuf, buffer, size);
  kbuf[size] = '\0';

  klog_print_str(kbuf);

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