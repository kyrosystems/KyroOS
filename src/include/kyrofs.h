#ifndef KYROFS_H
#define KYROFS_H

#include "vfs.h"

void kyrofs_init();
int kyrofs_add_file(char *path, void *data, uint32_t size);
vfs_node_t *get_kyrofs_root();

int kyrofs_create_node(vfs_node_t *parent, char *name, uint32_t flags);

#endif // KYROFS_H
