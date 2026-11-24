#include "vfs.h"
#include "log.h"
#include "heap.h"
#include "kstring.h"
#include "thread.h"
#include "scheduler.h"
#include "errno.h"

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
    vfs_root->uid = vfs_root->gid = 0;
    vfs_root->mode = DEFAULT_DIR_MODE;
}

// ---- POSIX-style permission checks (kernel/VFS level, not shell level) ----

static uint16_t vfs_effective_mode(vfs_node_t *node) {
    // Nodes created before permissions existed get defaults lazily.
    if (node->mode == 0) {
        node->mode = (node->flags & VFS_DIRECTORY) ? DEFAULT_DIR_MODE : DEFAULT_FILE_MODE;
    }
    return node->mode & 0777;
}

void vfs_node_set_owner(vfs_node_t *node, uint32_t uid, uint32_t gid, uint16_t mode) {
    if (!node) return;
    node->uid = uid;
    node->gid = gid;
    if (mode & S_IFMT) mode &= ~S_IFMT; // keep type bits out of perm storage
    node->mode = mode & 0777;
}

uint32_t vfs_default_mode_for(uint32_t flags, thread_t *t) {
    uint32_t base = (flags & VFS_DIRECTORY) ? DEFAULT_DIR_MODE : DEFAULT_FILE_MODE;
    uint32_t umask_v = t ? t->umask : 022;
    return base & ~umask_v;
}

// amode: bitmask 4=R, 2=W, 1=X. Returns 0 on allow, -errno on deny.
int vfs_check_access(vfs_node_t *node, int amode) {
    if (!node) return -ENOENT;
    thread_t *t = get_current_thread();
    if (!t) return 0; // early boot / kernel-internal access before threading
    if (t->euid == 0) return 0;

    uint16_t perms = vfs_effective_mode(node);
    uint32_t shift;
    if (t->euid == node->uid)      shift = 6;
    else if (t->egid == node->gid) shift = 3;
    else                           shift = 0;
    uint32_t bits = (perms >> shift) & 7;
    if ((amode & 4) && !(bits & 4)) return -EACCES;
    if ((amode & 2) && !(bits & 2)) return -EACCES;
    if ((amode & 1) && !(bits & 1)) return -EACCES;
    return 0;
}

// May the current thread create/remove entries in this directory?
int vfs_check_parent_write(vfs_node_t *dir) {
    if (!dir) return -ENOENT;
    if (!(dir->flags & VFS_DIRECTORY)) return -ENOTDIR;
    return vfs_check_access(dir, 2);
}

// Execute (X) check used by exec paths.
int vfs_check_exec(vfs_node_t *node) {
    if (!node) return -ENOENT;
    if (node->flags & VFS_DIRECTORY) return -EISDIR;
    return vfs_check_access(node, 1);
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
    if (!node) return 0;
    int ac = vfs_check_access(node, 4);
    if (ac != 0) return (uint32_t)-1;
    if (node->read) return node->read(node, offset, size, buffer);
    return 0;
}

uint32_t vfs_write(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    if (!node) return 0;
    int ac = vfs_check_access(node, 2);
    if (ac != 0) return (uint32_t)-1;
    if (node->write) return node->write(node, offset, size, buffer);
    return 0;
}

int vfs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
    if (node && (node->flags & VFS_DIRECTORY) && node->readdir) {
        int ac = vfs_check_access(node, 4);
        if (ac != 0) return -1;
        return node->readdir(node, index, dir_entry);
    }
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
    buf[255] = '\0';
    vfs_node_t *current = (path[0] == '/') ? vfs_root : (root ? root : vfs_root);
    char *token = (path[0] == '/') ? buf + 1 : buf;
    char *next = strchr(token, '/');
    while (token) {
        if (next) *next = '\0';
        if (*token != '\0') {
            if (strcmp(token, ".") == 0) {
                // stay at current node
            } else if (strcmp(token, "..") == 0) {
                vfs_node_t *p = vfs_finddir(current, "..");
                if (p) current = p;
            } else {
                current = vfs_finddir(current, token);
                if (!current) return NULL;
            }
        }
        if (!next) break;
        token = next + 1; next = strchr(token, '/');
    }
    return current;
}

int vfs_mkdir(vfs_node_t *parent, const char *name, uint16_t mode) {
    if (!parent) return -ENOENT;
    int ac = vfs_check_parent_write(parent);
    if (ac != 0) return ac;
    if (parent->mkdir) return parent->mkdir(parent, (char*)name, mode);
    return -1;
}

int vfs_create(vfs_node_t *parent, const char *name, uint16_t mode) {
    if (!parent) return -ENOENT;
    int ac = vfs_check_parent_write(parent);
    if (ac != 0) return ac;
    if (parent->create) return parent->create(parent, (char*)name, mode);
    return -1;
}

// Resolve parent directory and basename for path relative to root/parent.
// Returns 0 on success, -errno on failure.
static int vfs_resolve_parent_and_name(vfs_node_t *parent, const char *path,
                                       vfs_node_t **out_parent, char *out_name,
                                       size_t out_name_sz) {
    if (!parent || !path || path[0] == '\0') return -ENOENT;
    size_t plen = strlen(path);
    if (plen >= MAX_FILENAME_LEN) return -ENAMETOOLONG;

    char buf[MAX_FILENAME_LEN];
    memcpy(buf, path, plen + 1);
    while (plen > 1 && buf[plen - 1] == '/') {
        buf[--plen] = '\0';
    }
    if (strcmp(buf, "/") == 0) return -EBUSY;

    char *last_slash = strrchr(buf, '/');
    vfs_node_t *parent_dir;
    const char *name;
    if (!last_slash) {
        parent_dir = parent;
        name = buf;
    } else if (last_slash == buf) {
        parent_dir = vfs_root;
        name = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent_dir = vfs_resolve_path(parent, buf);
        name = last_slash + 1;
    }

    if (!parent_dir) return -ENOENT;
    if (!(parent_dir->flags & VFS_DIRECTORY)) return -ENOTDIR;
    if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -EINVAL;
    if (strlen(name) >= out_name_sz) return -ENAMETOOLONG;

    strncpy(out_name, name, out_name_sz - 1);
    out_name[out_name_sz - 1] = '\0';
    *out_parent = parent_dir;
    return 0;
}

int vfs_remove(vfs_node_t *parent, const char *path) {
    vfs_node_t *parent_dir = NULL;
    char basename[MAX_FILENAME_LEN];
    int rc = vfs_resolve_parent_and_name(parent, path, &parent_dir, basename, sizeof(basename));
    if (rc != 0) return rc;

    int ac = vfs_check_parent_write(parent_dir);
    if (ac != 0) return ac;

    vfs_node_t *victim = vfs_finddir(parent_dir, basename);
    if (!victim) return -ENOENT;

    thread_t *caller = get_current_thread();
    if ((parent_dir->mode & 01000) && caller && caller->euid != 0 &&
        caller->euid != parent_dir->uid && caller->euid != victim->uid) return -EPERM;

    if (victim->flags & VFS_DIRECTORY) return -EISDIR;
    if (parent_dir->remove) return parent_dir->remove(parent_dir, basename);
    return -ENOSYS;
}

int vfs_rmdir(vfs_node_t *parent, const char *path, uint16_t mode) {
    vfs_node_t *parent_dir = NULL;
    char basename[MAX_FILENAME_LEN];
    int rc = vfs_resolve_parent_and_name(parent, path, &parent_dir, basename, sizeof(basename));
    if (rc != 0) return rc;

    int ac = vfs_check_parent_write(parent_dir);
    if (ac != 0) return ac;

    vfs_node_t *victim = vfs_finddir(parent_dir, basename);
    if (!victim) return -ENOENT;

    thread_t *caller = get_current_thread();
    if ((parent_dir->mode & 01000) && caller && caller->euid != 0 &&
        caller->euid != parent_dir->uid && caller->euid != victim->uid) return -EPERM;

    if (!(victim->flags & VFS_DIRECTORY)) return -ENOTDIR;
    if (parent_dir->rmdir) return parent_dir->rmdir(parent_dir, basename, mode);
    return -ENOSYS;
}

uint32_t vfs_get_next_inode() { return next_inode++; }