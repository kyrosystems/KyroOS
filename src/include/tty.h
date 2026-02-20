#ifndef TTY_H
#define TTY_H

#include "vfs.h"
#include <stdint.h>

uint32_t tty_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                  uint8_t *buffer);
uint32_t tty_write(vfs_node_t *node, uint64_t offset, uint32_t size,
                   uint8_t *buffer);
void tty_init();

#endif // TTY_H