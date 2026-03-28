#include "kyrofs.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "vfs.h"
#include "fs_disk.h"
#include "thread.h"
#include "mutex.h"
#include <stddef.h>
#include <stdint.h>
#include "tty.h"

// In-memory directory entry structure (different from on-disk format)
typedef struct kyrofs_ino_dirent {
  vfs_node_t node;
  struct vfs_node *parent; // Pointer to parent directory node
  struct kyrofs_ino_dirent *next;
} kyrofs_ino_dirent_t;

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
    kyrofs_ino_dirent_t *de =
        (kyrofs_ino_dirent_t *)node; // Hack as node is first member
    // In our root_node init, node is the first member, so this works.
    // However, kyrofs_init root_node->parent might be NULL or self.
    if (de->parent)
      return de->parent;
    return node; // Root returns self for ..
  }

  kyrofs_ino_dirent_t *current = (kyrofs_ino_dirent_t *)node->ptr;
  while (current) {
    if (strcmp(current->node.name, name) == 0)
      return &current->node;
    current = current->next;
  }
  return NULL;
}

static int kyrofs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
  kyrofs_ino_dirent_t *current = (kyrofs_ino_dirent_t *)node->ptr;
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
  klog(LOG_DEBUG, "kyrofs_create_node: Allocating dirent for '%s' (parent %p)", name, parent);
  kyrofs_ino_dirent_t *new_de = (kyrofs_ino_dirent_t *)kmalloc(sizeof(kyrofs_ino_dirent_t));
  if (!new_de) {
      panic("kyrofs_create_node: kmalloc failed for dirent", NULL);
  }
  klog(LOG_DEBUG, "kyrofs_create_node: dirent for '%s' allocated at %p", name, new_de);
  memset(new_de, 0, sizeof(kyrofs_ino_dirent_t));
  strncpy(new_de->node.name, name, MAX_FILENAME_LEN - 1);
  new_de->node.name[MAX_FILENAME_LEN - 1] = '\0';
  new_de->node.flags = flags;
  new_de->node.inode = vfs_get_next_inode(); // Assign a unique inode number

  if (flags & VFS_FILE) {
    klog(LOG_DEBUG, "kyrofs_create_node: Allocating file_content for '%s'", name);
    kyrofs_file_content_t *content =
        (kyrofs_file_content_t *)kmalloc(sizeof(kyrofs_file_content_t));
    if (!content) {
        panic("kyrofs_create_node: kmalloc failed for file_content struct", NULL);
    }
    klog(LOG_DEBUG, "kyrofs_create_node: file_content for '%s' allocated at %p", name, content);
    content->size = 0;
    content->capacity = 128;
    klog(LOG_DEBUG, "kyrofs_create_node: Allocating initial file buffer for '%s', capacity %u", name, content->capacity);
    content->content = (uint8_t *)kmalloc(content->capacity);
    if (!content->content) {
        panic("kyrofs_create_node: kmalloc failed for initial file buffer", NULL);
    }
    klog(LOG_DEBUG, "kyrofs_create_node: Initial file buffer for '%s' allocated at %p", name, content->content);
    new_de->node.ptr = content;
    new_de->node.read = kyrofs_read;
    new_de->node.write = kyrofs_write;
    new_de->node.open = kyrofs_open; // Assign the open function
    new_de->node.close = NULL; // Optional: specific close for files
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
  new_de->next = (kyrofs_ino_dirent_t *)parent->ptr;
  parent->ptr = new_de;
  klog(LOG_DEBUG, "kyrofs_create_node: Node '%s' created and linked. New parent->ptr %p", name, parent->ptr);
  return 0;
}

static int kyrofs_mkdir(vfs_node_t *node, char *name, uint16_t mode) {
  (void)mode;
  // Check if directory already exists
  if (vfs_finddir(node, name)) {
      klog(LOG_WARN, "kyrofs_mkdir: Directory '%s' already exists.", name);
      return -1; // Or return 0 if idempotent
  }
  return kyrofs_create_node(node, name, VFS_DIRECTORY);
}

static int kyrofs_create(vfs_node_t *node, char *name, uint16_t mode) {
  (void)mode;
  return kyrofs_create_node(node, name, VFS_FILE);
}

static int kyrofs_remove(vfs_node_t *node, char *name) {
  kyrofs_ino_dirent_t *current = (kyrofs_ino_dirent_t *)node->ptr;
  kyrofs_ino_dirent_t *prev = NULL;

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
    kyrofs_ino_dirent_t *current = (kyrofs_ino_dirent_t *)node->ptr;
    kyrofs_ino_dirent_t *prev = NULL;

    while (current) {
        if (strcmp(current->node.name, name) == 0) {
            if (!(current->node.flags & VFS_DIRECTORY)) {
                return -1; // Not a directory
            }
            if (((kyrofs_ino_dirent_t*)current->node.ptr) != NULL) { // Check if directory has contents
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

static kyrofs_ino_dirent_t *root_node = NULL;

// Helper function to recursively create directories
vfs_node_t *kyrofs_create_dir_recursive(vfs_node_t *current_root, const char *path) {
    if (!path || !*path || strcmp(path, "/") == 0) {
        return current_root; // Empty path, null, or just root, return current_root
    }

    char temp_path[MAX_FILENAME_LEN];
    strncpy(temp_path, path, MAX_FILENAME_LEN - 1);
    temp_path[MAX_FILENAME_LEN - 1] = '\0';

    char *token_start = temp_path;
    if (*token_start == '/') {
        token_start++; // Skip leading slash if present
    }

    vfs_node_t *current_node = current_root;

    while (*token_start) {
        char *token_end = token_start;
        while (*token_end && *token_end != '/') {
            token_end++;
        }

        char name[MAX_FILENAME_LEN];
        int len = token_end - token_start;
        if (len >= MAX_FILENAME_LEN) {
            klog(LOG_ERROR, "kyrofs_create_dir_recursive: Path component too long.");
            return NULL;
        }
        strncpy(name, token_start, len);
        name[len] = '\0';

        if (len == 0) { // Handle multiple slashes like /a//b
            token_start = token_end;
            if (*token_start == '/') {
                token_start++;
            }
            continue;
        }

        vfs_node_t *child = vfs_finddir(current_node, name);
        if (!child) {
            // Directory does not exist, create it
            klog(LOG_INFO, "kyrofs_create_dir_recursive: Creating directory '%s'", name);
            kyrofs_create_node(current_node, name, VFS_DIRECTORY);
            child = vfs_finddir(current_node, name); // Find the newly created directory
            if (!child) {
                klog(LOG_ERROR, "kyrofs_create_dir_recursive: Failed to create directory '%s'", name);
                return NULL;
            }
        } else if (!(child->flags & VFS_DIRECTORY)) {
            klog(LOG_ERROR, "kyrofs_create_dir_recursive: Path component '%s' is not a directory.", name);
            return NULL; // Path component is a file, not a directory
        }
        current_node = child;
        token_start = token_end;
        if (*token_start == '/') {
            token_start++;
        }
    }
    return current_node;
}


int kyrofs_add_file(const char *full_path, void *data, uint32_t size) {
    if (!full_path || !*full_path || full_path[0] != '/') {
        klog(LOG_ERROR, "kyrofs_add_file: Invalid full path '%s'", full_path);
        return -1; // Must be an absolute path
    }

    char path_copy[MAX_FILENAME_LEN];
    strncpy(path_copy, full_path, MAX_FILENAME_LEN - 1); // Fix strncpy null-termination
    path_copy[MAX_FILENAME_LEN - 1] = '\0'; // Ensure null termination

    // Find the last slash to separate parent path and filename
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) { // This should not happen for absolute paths (e.g., /file)
        klog(LOG_ERROR, "kyrofs_add_file: Malformed full path (no slash) '%s'", full_path);
        return -1;
    }

    char *filename = last_slash + 1;
    *last_slash = '\0'; // Null-terminate parent path

    const char *parent_dir_path = path_copy;
    if (parent_dir_path[0] == '\0') { // Case of root directory, e.g., "/file" -> parent is "/"
        parent_dir_path = "/";
    }

    vfs_node_t *parent_node = kyrofs_create_dir_recursive(vfs_root, parent_dir_path);
    if (!parent_node) {
        klog(LOG_ERROR, "kyrofs_add_file: Failed to create/find parent directory for '%s'", full_path);
        return -1;
    }

    // Check if file already exists
    if (vfs_finddir(parent_node, filename)) {
        klog(LOG_WARN, "kyrofs_add_file: File '%s' already exists, overwriting.", full_path);
        kyrofs_remove(parent_node, filename); // Remove existing file
    }

    kyrofs_create_node(parent_node, filename, VFS_FILE);
    vfs_node_t *file_node = vfs_finddir(parent_node, filename);
    if (!file_node) {
        klog(LOG_ERROR, "kyrofs_add_file: Failed to create file node for '%s'", full_path);
        return -1;
    }

    kyrofs_write(file_node, 0, size, (uint8_t *)data);
    file_node->length = size; // Ensure length is updated
    klog(LOG_INFO, "kyrofs_add_file: Added file '%s' with size %u", full_path, size);
    return 0;
}

void kyrofs_init(struct limine_module_response *mod_resp) {
  klog(LOG_DEBUG, "KyroFS: Initializing in-memory filesystem.");
  root_node = (kyrofs_ino_dirent_t *)kmalloc(sizeof(kyrofs_ino_dirent_t));
  if (!root_node) {
      panic("kyrofs_init: kmalloc failed for root_node", NULL);
  }
  klog(LOG_DEBUG, "KyroFS: root_node allocated at %p", root_node);

  memset(root_node, 0, sizeof(kyrofs_ino_dirent_t));
  klog(LOG_DEBUG, "KyroFS: root_node memset to 0.");

  strncpy(root_node->node.name, "/", MAX_FILENAME_LEN - 1);
  root_node->node.name[MAX_FILENAME_LEN - 1] = '\0';
  root_node->node.flags = VFS_DIRECTORY;
  root_node->node.inode = vfs_get_next_inode(); // Assign a unique inode for root
  root_node->node.finddir = kyrofs_finddir;
  root_node->node.readdir = kyrofs_readdir;
  root_node->node.mkdir = kyrofs_mkdir;
  root_node->node.create = kyrofs_create;
  root_node->node.remove = kyrofs_remove;
  root_node->node.rmdir = kyrofs_rmdir; // Assign new rmdir function
  root_node->node.stat = kyrofs_stat; // Assign stat function
  root_node->node.ioctl = kyrofs_ioctl; // Assign new ioctl function
  root_node->node.open = kyrofs_open; // Assign the open function
  root_node->node.ptr = NULL; // Head of dirent list for root
  root_node->parent = &root_node->node; // Root's parent is root

  vfs_root = &root_node->node;
  klog(LOG_DEBUG, "KyroFS: vfs_root set to %p", vfs_root);

  // Create some initial top-level directories
  kyrofs_mkdir(vfs_root, "bin", 0);
  kyrofs_mkdir(vfs_root, "etc", 0);
  kyrofs_mkdir(vfs_root, "var", 0);
  kyrofs_mkdir(vfs_root, "mnt", 0);
  kyrofs_mkdir(vfs_root, "dev", 0);
  kyrofs_mkdir(vfs_root, "docs", 0);
  klog(LOG_DEBUG, "KyroFS: Top-level directories created.");

  // Process Limine modules
  if (mod_resp != NULL) {
      for (uint64_t i = 0; i < mod_resp->module_count; i++) {
          struct limine_file *module = mod_resp->modules[i];
          const char *full_path = module->path;
          
          // Limine path is usually something like "/bin/hello" or "boot():/bin/hello"
          // We want to strip the "boot():" part if it exists
          const char *vfs_path = full_path;
          const char *colon = strchr(full_path, ':');
          if (colon) {
              vfs_path = colon + 1;
          }

          klog(LOG_INFO, "KyroFS: Loading module %s to %s", full_path, vfs_path);
          kyrofs_add_file(vfs_path, module->address, (uint32_t)module->size);
      }
  }

  // Pre-load some system info docs
  kyrofs_add_file("/docs/readme.txt", "KyroOS Titanium\nWelcome to the future.", 36);
  kyrofs_add_file("/docs/version.txt", OS_VERSION, strlen(OS_VERSION));

  // Create /dev/tty
  klog(LOG_DEBUG, "KyroFS: Creating /dev/tty");
  vfs_node_t *dev_dir = vfs_finddir(vfs_root, "dev");
  if (dev_dir) {
      kyrofs_create_node(dev_dir, "tty", VFS_CHARDEVICE);
      vfs_node_t* tty_node = vfs_finddir(dev_dir, "tty");
      if (tty_node) {
        tty_node->read = tty_read;
        tty_node->write = tty_write;
      }
      klog(LOG_DEBUG, "KyroFS: /dev/tty created.");
  } else {
      klog(LOG_ERROR, "KyroFS: Failed to find /dev directory for tty creation.");
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
