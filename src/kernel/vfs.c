#include "vfs.h"
#include "kstring.h" // Moved to top
#include "log.h"
#include "thread.h" // For current_thread and fd_entry_t

// #include <stddef.h> // Removed, as kstring.h includes it

vfs_node_t *vfs_root = NULL;

void vfs_init() {
  // The root is set by mounting a filesystem, e.g., KyroFS
  klog(LOG_INFO, "VFS initialized.");
}

void vfs_open(vfs_node_t *node, int flags) {
  if (node && node->open) {
    node->open(node, flags);
  }
}

int vfs_stat(vfs_node_t *root, const char *path, struct stat *stat_buf) {
  vfs_node_t *node = vfs_resolve_path(root, path);
  if (!node) {
    return -1;
  }
  if (node->stat) {
    return node->stat(node, stat_buf);
  }
  return -1;
}

uint32_t vfs_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                  uint8_t *buffer) {
  if (node && node->read) {
    return node->read(node, offset, size, buffer);
  }
  return 0;
}

uint32_t vfs_write(vfs_node_t *node, uint64_t offset, uint32_t size,
                   uint8_t *buffer) {
  if (node && node->write) {
    return node->write(node, offset, size, buffer);
  }
  return 0;
}

vfs_node_t *vfs_finddir(vfs_node_t *node, char *name) {
  if (node && (node->flags & VFS_DIRECTORY) && node->finddir) {
    return node->finddir(node, name);
  }
  return NULL;
}

int vfs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
  if (node && (node->flags & VFS_DIRECTORY) && node->readdir) {
    return node->readdir(node, index, dir_entry);
  }
  return 0; // Return 0 to indicate end of directory or error
}

int vfs_mkdir(vfs_node_t *root, const char *path, uint16_t mode) {
  if (!path || !root)
    return -1;

  char parent_path[MAX_FILENAME_LEN];
  char *target_name;

  int last_slash = -1;
  for (int i = 0; path[i]; i++) {
    if (path[i] == '/')
      last_slash = i;
  }

  if (last_slash == -1) {
    target_name = (char *)path;
    return (root->mkdir) ? root->mkdir(root, target_name, mode) : -1;
  } else if (last_slash == 0) {
    target_name = (char *)(path + 1);
    return (root->mkdir) ? root->mkdir(root, target_name, mode) : -1;
  }

  memcpy(parent_path, path, last_slash);
  parent_path[last_slash] = '\0';
  target_name = (char *)(path + last_slash + 1);

  vfs_node_t *parent_node = vfs_resolve_path(root, parent_path);
  if (!parent_node || !(parent_node->flags & VFS_DIRECTORY) ||
      !parent_node->mkdir) {
    return -1;
  }

  return parent_node->mkdir(parent_node, target_name, mode);
}

int vfs_create(vfs_node_t *root, const char *path, uint16_t mode) {
  if (!path || !root)
    return -1;

  char parent_path[MAX_FILENAME_LEN];
  char *target_name;

  int last_slash = -1;
  for (int i = 0; path[i]; i++) {
    if (path[i] == '/')
      last_slash = i;
  }

  if (last_slash == -1) {
    target_name = (char *)path;
    return (root->create) ? root->create(root, target_name, mode) : -1;
  } else if (last_slash == 0) {
    target_name = (char *)(path + 1);
    return (root->create) ? root->create(root, target_name, mode) : -1;
  }

  memcpy(parent_path, path, last_slash);
  parent_path[last_slash] = '\0';
  target_name = (char *)(path + last_slash + 1);

  vfs_node_t *parent_node = vfs_resolve_path(root, parent_path);
  if (!parent_node || !(parent_node->flags & VFS_DIRECTORY) ||
      !parent_node->create) {
    return -1;
  }

  return parent_node->create(parent_node, target_name, mode);
}

int vfs_remove(vfs_node_t *root, const char *path) {
  if (!path || !root)
    return -1;

  char parent_path[MAX_FILENAME_LEN];
  char *target_name;

  int last_slash = -1;
  for (int i = 0; path[i]; i++) {
    if (path[i] == '/')
      last_slash = i;
  }

  if (last_slash == -1) {
    // No slash, removing item in the root directory
    target_name = (char *)path;
    return (root->remove) ? root->remove(root, target_name) : -1;
  } else if (last_slash == 0) {
    // Path is like "/file", parent is root
    target_name = (char *)(path + 1);
    return (root->remove) ? root->remove(root, target_name) : -1;
  }

  // Path is like "/a/b/c", need to find parent "/a/b"
  memcpy(parent_path, path, last_slash);
  parent_path[last_slash] = '\0';
  target_name = (char *)(path + last_slash + 1);

  vfs_node_t *parent_node = vfs_resolve_path(root, parent_path);
  if (!parent_node || !(parent_node->flags & VFS_DIRECTORY) ||
      !parent_node->remove) {
    return -1;
  }

  return parent_node->remove(parent_node, target_name);
}

int vfs_rmdir(vfs_node_t *root, const char *path, uint16_t mode) {
  if (!path || !root)
    return -1;

  char parent_path[MAX_FILENAME_LEN];
  char *target_name;

  int last_slash = -1;
  for (int i = 0; path[i]; i++) {
    if (path[i] == '/')
      last_slash = i;
  }

  if (last_slash == -1) {
    target_name = (char *)path;
    return (root->rmdir) ? root->rmdir(root, target_name, mode) : -1;
  } else if (last_slash == 0) {
    target_name = (char *)(path + 1);
    return (root->rmdir) ? root->rmdir(root, target_name, mode) : -1;
  }

  memcpy(parent_path, path, last_slash);
  parent_path[last_slash] = '\0';
  target_name = (char *)(path + last_slash + 1);

  vfs_node_t *parent_node = vfs_resolve_path(root, parent_path);
  if (!parent_node || !(parent_node->flags & VFS_DIRECTORY) ||
      !parent_node->rmdir) {
    return -1;
  }

  return parent_node->rmdir(parent_node, target_name, mode);
}

#include "kstring.h"
vfs_node_t *vfs_resolve_path(vfs_node_t *root, const char *path) {
  if (!path || !root)
    return NULL;

  if (path[0] == '/') {
    path++;
    if (!*path)
      return root;
  }

  char buf[MAX_FILENAME_LEN];
  const char *start = path;
  vfs_node_t *current = root;

  while (*start) {
    const char *end = start;
    while (*end && *end != '/')
      end++;

    int len = end - start;
    if (len > 0) {
      memcpy(buf, start, len);
      buf[len] = '\0';

      current = vfs_finddir(current, buf);
      if (!current)
        return NULL;
    }

    if (!*end)
      break;
    start = end + 1;
  }

  return current;
}

// Kernel-internal ioctl that dispatches to the VFS node associated with a file
// descriptor
extern thread_t *current_thread; // Defined in thread.c

int kernel_ioctl(int fd, int request, void *argp) {
  if (fd < 0 || fd >= MAX_FILES) {
    klog(LOG_ERROR, "VFS: kernel_ioctl: Invalid file descriptor %d.", fd);
    return -1;
  }

  if (!current_thread) {
    klog(LOG_ERROR, "VFS: kernel_ioctl: No current thread available.");
    return -1;
  }

  fd_entry_t *fde = &current_thread->fd_table[fd];

  if (fde->type == FD_TYPE_FILE && fde->data.file.node &&
      fde->data.file.node->ioctl) {
    return fde->data.file.node->ioctl(fde->data.file.node, request, argp);
  }
  // Handle other types of FDs (sockets, etc.) here if needed


  klog(LOG_WARN, "VFS: kernel_ioctl: No ioctl handler for fd %d (type %d).", fd,
       fde->type);
  return -1;
}
