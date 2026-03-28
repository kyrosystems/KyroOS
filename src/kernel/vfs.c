#include "vfs.h"
#include "log.h"
#include "heap.h"
#include "kstring.h"

vfs_node_t *vfs_root = NULL;
static uint32_t next_inode = 1;

void vfs_init() {
    vfs_root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    memset(vfs_root, 0, sizeof(vfs_node_t));
    strncpy(vfs_root->name, "/", 2);
    vfs_root->flags = VFS_DIRECTORY;
    vfs_root->inode = next_inode++;
}

uint32_t vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    if (node && node->read) return node->read(node, offset, size, buffer);
    return 0;
}

uint32_t vfs_write(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    if (node && node->write) return node->write(node, offset, size, buffer);
    return 0;
}

int vfs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
    if (node && (node->flags & VFS_DIRECTORY) && node->readdir)
        return node->readdir(node, index, dir_entry);
    return -1;
}

vfs_node_t *vfs_finddir(vfs_node_t *node, char *name) {
    if (node && (node->flags & VFS_DIRECTORY) && node->finddir)
        return node->finddir(node, name);
    return NULL;
}

vfs_node_t *vfs_resolve_path(vfs_node_t *root, const char *path) {
    if (!path || path[0] == '\0') return root;
    if (strcmp(path, "/") == 0) return vfs_root;
    char buf[256]; strncpy(buf, path, 255);
    vfs_node_t *current = (path[0] == '/') ? vfs_root : root;
    char *token = (path[0] == '/') ? buf + 1 : buf;
    char *next = strchr(token, '/');
    while (token) {
        if (next) *next = '\0';
        current = vfs_finddir(current, token);
        if (!current) return NULL;
        if (!next) break;
        token = next + 1; next = strchr(token, '/');
    }
    return current;
}

int vfs_mkdir(vfs_node_t *parent, const char *name, uint16_t mode) {
    if (parent && parent->mkdir) return parent->mkdir(parent, (char*)name, mode);
    return -1;
}

int vfs_create(vfs_node_t *parent, const char *name, uint16_t mode) {
    if (parent && parent->create) return parent->create(parent, (char*)name, mode);
    return -1;
}

int vfs_remove(vfs_node_t *parent, const char *path) { (void)parent; (void)path; return 0; }
int vfs_rmdir(vfs_node_t *parent, const char *path, uint16_t mode) { (void)parent; (void)path; (void)mode; return 0; }
uint32_t vfs_get_next_inode() { return next_inode++; }
