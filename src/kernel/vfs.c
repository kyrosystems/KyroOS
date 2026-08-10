#include "vfs.h"
#include "log.h"
#include "heap.h"
#include "kstring.h"

vfs_node_t *vfs_root = NULL;
static uint32_t next_inode = 1;

// fs list
static struct filesystem_type *fs_list = NULL;

void vfs_init() {
    vfs_root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    memset(vfs_root, 0, sizeof(vfs_node_t));
    strncpy(vfs_root->name, "/", 2);
    vfs_root->flags = VFS_DIRECTORY;
    vfs_root->inode = next_inode++;
}

// reg fs
void register_filesystem(struct filesystem_type *fs) {
    if (!fs) return;
    fs->next = fs_list;
    fs_list = fs;
}

// mount fs
int vfs_mount(vfs_node_t *mount_point, vfs_node_t *device_node, const char* fs_type_name) {
    if (!mount_point || !fs_type_name) return -1;
    struct filesystem_type *fs = fs_list;
    while (fs) {
        if (strcmp(fs->name, fs_type_name) == 0) {
            if (fs->mount_func) return fs->mount_func(mount_point, device_node);
            return -1;
        }
        fs = fs->next;
    }
    return -1;
}

// umount fs
int vfs_unmount(vfs_node_t *mount_point) {
    if (!mount_point) return -1;
    mount_point->ptr = NULL;
    return 0;
}

// chk stub
char *__strncpy_chk(char *dest, const char *src, size_t len, size_t destlen) {
    (void)destlen;
    return strncpy(dest, src, len);
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