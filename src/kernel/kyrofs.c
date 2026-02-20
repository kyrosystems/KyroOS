#include "kyrofs.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "vfs.h"
#include "fs_disk.h" // For fs_unmount
#include <stddef.h>
#include <stdint.h>
#include "tty.h"

typedef struct kyrofs_dirent {
  vfs_node_t node;
  struct vfs_node *parent; // Pointer to parent directory node
  struct kyrofs_dirent *next;
} kyrofs_dirent_t;

typedef struct {
  uint8_t *content;
  uint32_t size;
  uint32_t capacity;
} kyrofs_file_content_t;

// Forward declarations
static int kyrofs_mkdir(vfs_node_t *node, char *name, uint16_t mode);
static int kyrofs_create(vfs_node_t *node, char *name, uint16_t mode);
static int kyrofs_remove(vfs_node_t *node, char *name);
static int kyrofs_rmdir(vfs_node_t *node, char *name, uint16_t mode);
static int kyrofs_stat(vfs_node_t *node, struct stat *stat_buf);
static int kyrofs_ioctl(vfs_node_t *node, int request, void* argp);

static void kyrofs_open(vfs_node_t *node, int flags) {
    if (node->flags & VFS_FILE) {
        if (flags & O_TRUNC) {
            kyrofs_file_content_t *file_content = (kyrofs_file_content_t *)node->ptr;
            // Free existing content if any
            if (file_content->content) {
                kfree(file_content->content);
            }
            // Reallocate initial capacity if needed, or just set to NULL and size 0
            file_content->content = NULL; // Mark as empty
            file_content->size = 0;
            node->length = 0;
            // Optionally reallocate with initial capacity if a non-zero capacity is desired on truncate

        }
        // In a real FS, read/write flags might affect access checks.
        // For in-memory KyroFS, we allow read/write always once opened.
    }
}

static uint32_t kyrofs_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                            uint8_t *buffer) {
  kyrofs_file_content_t *file_content = (kyrofs_file_content_t *)node->ptr;
  if (!file_content || !file_content->content) {
      klog(LOG_ERROR, "kyrofs_read: Attempt to read from NULL file_content or content pointer.");
      return 0;
  }
  if (offset >= file_content->size)
    return 0;
  if (offset + size > file_content->size)
    size = file_content->size - offset;
  memcpy(buffer, file_content->content + offset, size);
  return size;
}

static uint32_t kyrofs_write(vfs_node_t *node, uint64_t offset, uint32_t size,
                             uint8_t *buffer) {
  kyrofs_file_content_t *file_content = (kyrofs_file_content_t *)node->ptr;
  if (offset + size > file_content->capacity) {
    uint32_t new_cap = (offset + size) * 2;
    uint8_t *new_cont = (uint8_t *)kmalloc(new_cap);
    if (!new_cont) {
        panic("kyrofs_write: kmalloc failed for new content buffer", NULL);
    }
    if (file_content->content) {
      memcpy(new_cont, file_content->content, file_content->size);
      kfree(file_content->content);
    }
    file_content->content = new_cont;
    file_content->capacity = new_cap;
  }
  memcpy(file_content->content + offset, buffer, size);
  if (offset + size > file_content->size)
    file_content->size = offset + size;
  node->length = file_content->size;
  return size;
}

static vfs_node_t *kyrofs_finddir(vfs_node_t *node, char *name) {
  if (strcmp(name, ".") == 0)
    return node;
  if (strcmp(name, "..") == 0) {
    kyrofs_dirent_t *de =
        (kyrofs_dirent_t *)node; // Hack as node is first member
    // In our root_node init, node is the first member, so this works.
    // However, kyrofs_init root_node->parent might be NULL or self.
    if (de->parent)
      return de->parent;
    return node; // Root returns self for ..
  }

  kyrofs_dirent_t *current = (kyrofs_dirent_t *)node->ptr;
  while (current) {
    if (strcmp(current->node.name, name) == 0)
      return &current->node;
    current = current->next;
  }
  return NULL;
}

static int kyrofs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
  kyrofs_dirent_t *current = (kyrofs_dirent_t *)node->ptr;
  uint32_t i = 0;
  while (current && i < index) {
    current = current->next;
    i++;
  }
  if (current) {
    strncpy(dir_entry->name, current->node.name, MAX_FILENAME_LEN);
    dir_entry->ino = current->node.inode;
    return 1; // Success
  }
  return 0; // End of directory or invalid index
}

int kyrofs_create_node(vfs_node_t *parent, char *name, uint32_t flags) {
  kyrofs_dirent_t *new_de = (kyrofs_dirent_t *)kmalloc(sizeof(kyrofs_dirent_t));
  if (!new_de) {
      panic("kyrofs_create_node: kmalloc failed for dirent", NULL);
  }
  memset(new_de, 0, sizeof(kyrofs_dirent_t));
  strncpy(new_de->node.name, name, MAX_FILENAME_LEN);
  new_de->node.flags = flags;

  if (flags & VFS_FILE) {
    kyrofs_file_content_t *content =
        (kyrofs_file_content_t *)kmalloc(sizeof(kyrofs_file_content_t));
    if (!content) {
        panic("kyrofs_create_node: kmalloc failed for file_content struct", NULL);
    }
    content->size = 0;
    content->capacity = 128;
    content->content = (uint8_t *)kmalloc(content->capacity);
    if (!content->content) {
        panic("kyrofs_create_node: kmalloc failed for initial file buffer", NULL);
    }
    new_de->node.ptr = content;
    new_de->node.read = kyrofs_read;
    new_de->node.write = kyrofs_write;
    new_de->node.open = kyrofs_open; // Assign the open function
  } else {
    new_de->node.ptr = NULL; // Head of dirent list for this dir
    new_de->node.finddir = kyrofs_finddir;
    new_de->node.readdir = kyrofs_readdir;
    new_de->node.mkdir = kyrofs_mkdir;
    new_de->node.create = kyrofs_create;
    new_de->node.remove = kyrofs_remove;
    new_de->node.rmdir = kyrofs_rmdir; // Assign new rmdir function
    new_de->node.stat = kyrofs_stat; // Assign new stat function
    new_de->node.ioctl = kyrofs_ioctl; // Assign new ioctl function
  }

  new_de->parent = parent;
  // Link into parent
  new_de->next = (kyrofs_dirent_t *)parent->ptr;
  parent->ptr = new_de;
  return 0;
}

static int kyrofs_mkdir(vfs_node_t *node, char *name, uint16_t mode) {
  (void)mode;
  return kyrofs_create_node(node, name, VFS_DIRECTORY);
}

static int kyrofs_create(vfs_node_t *node, char *name, uint16_t mode) {
  (void)mode;
  return kyrofs_create_node(node, name, VFS_FILE);
}

static int kyrofs_remove(vfs_node_t *node, char *name) {
  kyrofs_dirent_t *current = (kyrofs_dirent_t *)node->ptr;
  kyrofs_dirent_t *prev = NULL;

  while (current) {
    if (strcmp(current->node.name, name) == 0) {
      if ((current->node.flags & VFS_DIRECTORY) && current->node.ptr != NULL) {
        // Directory is not empty
        return -1; 
      }

      if (prev) {
        prev->next = current->next;
      } else {
        node->ptr = current->next;
      }

      if (current->node.flags & VFS_FILE) {
        kyrofs_file_content_t *content =
            (kyrofs_file_content_t *)current->node.ptr;
        if (content->content)
          kfree(content->content);
        kfree(content);
      }
      kfree(current);
      return 0;
    }
    prev = current;
    current = current->next;
  }
  return -1;
}

static int kyrofs_rmdir(vfs_node_t *node, char *name, uint16_t mode) {
    (void)mode;
    kyrofs_dirent_t *current = (kyrofs_dirent_t *)node->ptr;
    kyrofs_dirent_t *prev = NULL;

    while (current) {
        if (strcmp(current->node.name, name) == 0) {
            if (!(current->node.flags & VFS_DIRECTORY)) {
                return -1; // Not a directory
            }
            if (((kyrofs_dirent_t*)current->node.ptr) != NULL) { // Check if directory has contents
                return -1; // Directory is not empty
            }

            if (prev) {
                prev->next = current->next;
            } else {
                node->ptr = current->next;
            }
            kfree(current);
            return 0;
        }
        prev = current;
        current = current->next;
    }
    return -1; // Directory not found
}

static int kyrofs_stat(vfs_node_t *node, struct stat *stat_buf) {
    if (!node || !stat_buf) {
        return -1; // Invalid arguments
    }
    memset(stat_buf, 0, sizeof(struct stat)); // Clear stat_buf to prevent garbage

    stat_buf->st_size = node->length;
    stat_buf->st_ino = node->inode; // Use node->inode if available, or generate a simple one

    // Determine file type
    if (node->flags & VFS_FILE) {
        stat_buf->st_mode |= S_IFREG;
    } else if (node->flags & VFS_DIRECTORY) {
        stat_buf->st_mode |= S_IFDIR;
    } else if (node->flags & VFS_CHARDEVICE) {
        // You might define S_IFCHR in vfs.h if needed

        stat_buf->st_mode |= S_IFREG; // Treat char device as a special file for stat
    }
    // Add default permissions
    stat_buf->st_mode |= 0755; // rwxr-xr-x

    return 0;
}

// KyroFS itself does not support ioctl directly for its files/dirs
static int kyrofs_ioctl(vfs_node_t *node, int request, void* argp) {
    (void)node;
    (void)request;
    (void)argp;
    klog(LOG_WARN, "KyroFS: ioctl called on KyroFS node. Not supported.");
    return -1;
}

static kyrofs_dirent_t *root_node = NULL;

int kyrofs_add_file(char *path, void *data, uint32_t size) {
  // Use a simple path parser for '/'
  if (path[0] == '/')
    path++;

  char dir_name[MAX_FILENAME_LEN];
  char file_name[MAX_FILENAME_LEN];

  // Find last slash
  int last_slash = -1;
  for (int i = 0; path[i]; i++) {
    if (path[i] == '/')
      last_slash = i;
  }

  vfs_node_t *parent = vfs_root;
  if (last_slash != -1) {
    memcpy(dir_name, path, last_slash);
    dir_name[last_slash] = '\0';
    parent = vfs_finddir(vfs_root, dir_name);
    strncpy(file_name, path + last_slash + 1, MAX_FILENAME_LEN);
  } else {
    strncpy(file_name, path, MAX_FILENAME_LEN);
  }

  if (!parent)
    return -1;

  kyrofs_create(parent, file_name, 0);
  vfs_node_t *node = vfs_finddir(parent, file_name);
  if (!node)
    return -1;

  kyrofs_write(node, 0, size, (uint8_t *)data);
  return 0;
}

void kyrofs_init() {
  root_node = (kyrofs_dirent_t *)kmalloc(sizeof(kyrofs_dirent_t));
  if (!root_node) {
      panic("kyrofs_init: kmalloc failed for root_node", NULL);
  }
  memset(root_node, 0, sizeof(kyrofs_dirent_t));
  strncpy(root_node->node.name, "/", MAX_FILENAME_LEN);
  root_node->node.flags = VFS_DIRECTORY;
  root_node->node.finddir = kyrofs_finddir;
  root_node->node.readdir = kyrofs_readdir;
  root_node->node.mkdir = kyrofs_mkdir;
  root_node->node.create = kyrofs_create;
  root_node->node.remove = kyrofs_remove;
  root_node->node.rmdir = kyrofs_rmdir; // Assign new rmdir function
  root_node->node.stat = kyrofs_stat; // Assign stat function
  root_node->node.ioctl = kyrofs_ioctl; // Assign new ioctl function
  root_node->node.open = kyrofs_open; // Assign the open function
  root_node->node.ptr = NULL;
  root_node->parent = &root_node->node; // Root's parent is root

  vfs_root = &root_node->node;

  // Create some initial files
  kyrofs_mkdir(vfs_root, "bin", 0);
  kyrofs_mkdir(vfs_root, "etc", 0);
  kyrofs_mkdir(vfs_root, "var", 0);
  kyrofs_mkdir(vfs_root, "mnt", 0); // New: for mounting other filesystems
  kyrofs_mkdir(vfs_root, "dev", 0);

  vfs_node_t *var_node = vfs_finddir(vfs_root, "var");
  kyrofs_mkdir(var_node, "lib", 0);
  vfs_node_t *lib_node = vfs_finddir(var_node, "lib");
  kyrofs_mkdir(lib_node, "kpm", 0);

  // Create /dev/tty
  vfs_node_t *dev_dir = vfs_finddir(vfs_root, "dev");
  if (dev_dir) {
      kyrofs_create_node(dev_dir, "tty", VFS_CHARDEVICE);
      vfs_node_t* tty_node = vfs_finddir(dev_dir, "tty");
      if (tty_node) {
        tty_node->read = tty_read;
        tty_node->write = tty_write;
      }
  }

  klog(LOG_INFO, "KyroFS: In-memory filesystem initialized.");
}

// List of registered filesystem types
static struct filesystem_type *registered_filesystems = NULL;

void register_filesystem(struct filesystem_type *fs) {
    fs->next = registered_filesystems;
    registered_filesystems = fs;
    klog(LOG_INFO, "VFS: Registered filesystem: %s", fs->name);
}

int vfs_mount(vfs_node_t *mount_point, vfs_node_t *device_node, const char* fs_type_name) {
    if (!mount_point || !(mount_point->flags & VFS_DIRECTORY)) {
        klog(LOG_ERROR, "VFS: Mount point is not a directory or invalid.");
        return -1;
    }
    if (mount_point->flags & VFS_MOUNTPOINT) {
        klog(LOG_ERROR, "VFS: Mount point already busy.");
        return -1;
    }
    if (!device_node) {
        klog(LOG_ERROR, "VFS: Device node is invalid.");
        return -1;
    }

    struct filesystem_type *fs_type = registered_filesystems;
    while (fs_type) {
        if (strcmp(fs_type->name, fs_type_name) == 0) {
            return fs_type->mount_func(mount_point, device_node);
        }
        fs_type = fs_type->next;
    }

    klog(LOG_ERROR, "VFS: Filesystem type '%s' not found.", fs_type_name);
    return -1;
}

int vfs_unmount(vfs_node_t *mount_point) {
    if (!mount_point || !(mount_point->flags & VFS_MOUNTPOINT)) {
        klog(LOG_ERROR, "VFS: Not a mount point.");
        return -1;
    }
    // Need to find which filesystem is mounted here and call its unmount_func

    // This needs to be more robust for multiple FS types.
    if (fs_unmount(mount_point) != 0) {
        return -1;
    }
    mount_point->flags &= ~VFS_MOUNTPOINT;
    mount_point->ptr = NULL; // Clear pointer to mounted FS data
    return 0;
}