#ifndef DEVFS_H
#define DEVFS_H

#include <stdint.h>
#include "vfs.h" // Include vfs.h for vfs_node_t

// Function to initialize and register the devfs filesystem
void devfs_init();

// Mount function for devfs
int devfs_mount(vfs_node_t *mount_point, vfs_node_t *device_node);

// Specific read function for /dev/urandom
uint32_t urandom_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer);

#endif
