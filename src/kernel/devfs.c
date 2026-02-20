#include "devfs.h"
#include "errno.h" // For ENOMEM
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "random.h" // For random_get_bytes
#include "vfs.h"

// Global root node for devfs
static vfs_node_t *devfs_root_node;
// Global urandom node for easy access by devfs_finddir and devfs_readdir
static vfs_node_t *global_urandom_node = NULL;
static vfs_node_t *global_console_node = NULL;

// Helper to write to console
uint32_t console_write(vfs_node_t *node, uint64_t offset, uint32_t size,
                       uint8_t *buffer) {
  (void)node;
  (void)offset;
  char *str = (char *)kmalloc(size + 1);
  if (!str)
    return 0;
  memcpy(str, buffer, size);
  str[size] = '\0';
  klog_print_str(str); // Use klog_print_str equivalent
  kfree(str);
  return size;
}

// Helper to read from console (keyboard)
// This is a blocking read placeholder. Ideally should integrate with keyboard
// buffer.
extern char keyboard_get_char(); // Prototype, assuming exist or need include
uint32_t console_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                      uint8_t *buffer) {
  (void)node;
  (void)offset;
  (void)size; // Suppress unused parameter warning
  (void)buffer; // Suppress unused parameter warning

  // Always return 0 to disable console input
  return 0;
}

// --- urandom_read implementation ---
uint32_t urandom_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                      uint8_t *buffer) {
  (void)node;   // Unused parameter
  (void)offset; // /dev/urandom is a stream, offset is ignored

  // Generate random bytes directly into the buffer
  random_get_bytes(buffer, size);
  return size;
}

// --- devfs_finddir implementation ---
vfs_node_t *devfs_finddir(vfs_node_t *node, char *name) {
  (void)node; // Suppress unused parameter warning
  // if (node != devfs_root_node) {
  //   return NULL; // Only the devfs root has children handled by this finddir
  // }
  if (strcmp(name, "urandom") == 0) {
    return global_urandom_node;
  }
  if (strcmp(name, "console") == 0) {
    return global_console_node;
  }
  return NULL;
}

// --- devfs_readdir implementation ---
int devfs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
  (void)node; // Suppress unused parameter warning
  (void)index; // Suppress unused parameter warning

  if (!dir_entry) {
    return 0; // Invalid entry pointer
  }

  if (index == 0) {
    if (global_urandom_node) {
      strncpy(dir_entry->name, global_urandom_node->name, MAX_FILENAME_LEN);
      dir_entry->ino = global_urandom_node->inode;
      return 1; // Entry found
    }
  } else if (index == 1) {
    if (global_console_node) {
      strncpy(dir_entry->name, global_console_node->name, MAX_FILENAME_LEN);
      dir_entry->ino = global_console_node->inode;
      return 1;
    }
  }
  return 0; // No more entries
}

// --- devfs_mount implementation ---
int devfs_mount(vfs_node_t *mount_point, vfs_node_t *device_node) {
  (void)device_node; // DevFS does not mount a physical device

  if (!mount_point) {
    klog(LOG_ERROR, "DevFS: Mount point cannot be NULL.");
    return -EINVAL;
  }

  devfs_root_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  if (!devfs_root_node) {
    klog(LOG_ERROR, "DevFS: Failed to allocate root node.");
    return -ENOMEM;
  }

  // Initialize the devfs root node
  strncpy(devfs_root_node->name, "dev", MAX_FILENAME_LEN);
  devfs_root_node->flags = VFS_DIRECTORY; // Set directory flag
  // Permissions (0755) are not directly in flags; will be handled by stat

  devfs_root_node->inode = 0;   // Or some unique identifier
  devfs_root_node->length = 0;  // Directories don't have content length
  devfs_root_node->ptr = NULL; // No specific implementation data for the root
  devfs_root_node->open = NULL;
  devfs_root_node->close = NULL;
  devfs_root_node->read = NULL;
  devfs_root_node->write = NULL;
  devfs_root_node->finddir = devfs_finddir; // Custom finddir for devfs
  devfs_root_node->readdir = devfs_readdir; // Custom readdir for devfs
  devfs_root_node->create = NULL;
  devfs_root_node->mkdir = NULL;
  devfs_root_node->remove = NULL;
  devfs_root_node->rmdir = NULL;
  devfs_root_node->stat = NULL;
  devfs_root_node->ioctl = NULL;

  // Create /dev/urandom node
  vfs_node_t *urandom_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  if (!urandom_node) {
    klog(LOG_ERROR, "DevFS: Failed to allocate /dev/urandom node.");
    kfree(devfs_root_node);
    return -ENOMEM;
  }

  strncpy(urandom_node->name, "urandom", MAX_FILENAME_LEN);
  urandom_node->flags = VFS_CHARDEVICE; // Character device
  urandom_node->inode = 1;              // Or some unique identifier
  urandom_node->length = 0;             // Device nodes often have no fixed size
  urandom_node->ptr = NULL;            // No specific data needed for urandom
  urandom_node->open = NULL;
  urandom_node->close = NULL;
  urandom_node->read = urandom_read;
  urandom_node->write = NULL;
  urandom_node->finddir = NULL;
  urandom_node->readdir = NULL;
  urandom_node->create = NULL;
  urandom_node->mkdir = NULL;
  urandom_node->remove = NULL;
  urandom_node->rmdir = NULL;
  urandom_node->stat = NULL;
  urandom_node->ioctl = NULL;

  global_urandom_node = urandom_node;

  // The mount_point node (which is actually the /dev directory created in
  // KyroFS) needs to have its operations "redirected" to devfs_root_node's
  // operations. The vfs_mount function in vfs.c expects the mount_point to
  // become the root of the mounted filesystem from the VFS perspective. So, we
  // copy devfs_root_node's operations to mount_point.
  memcpy(mount_point->name, devfs_root_node->name, MAX_FILENAME_LEN);
  mount_point->flags = devfs_root_node->flags;
  mount_point->inode = devfs_root_node->inode;
  mount_point->length = devfs_root_node->length;
  mount_point->ptr = devfs_root_node->ptr; // This might be NULL
  mount_point->open = devfs_root_node->open;
  mount_point->close = devfs_root_node->close;
  mount_point->read = devfs_root_node->read;
  mount_point->write = devfs_root_node->write;
  mount_point->finddir = devfs_root_node->finddir; // Custom finddir for devfs
  mount_point->readdir = devfs_root_node->readdir; // Custom readdir for devfs
  mount_point->create = devfs_root_node->create;
  mount_point->mkdir = devfs_root_node->mkdir;
  mount_point->remove = devfs_root_node->remove;
  mount_point->rmdir = devfs_root_node->rmdir;
  mount_point->stat = devfs_root_node->stat;
  mount_point->ioctl = devfs_root_node->ioctl;

  klog(LOG_INFO, "DevFS: /dev/urandom node created.");

  // Create /dev/console node
  vfs_node_t *console_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  if (!console_node) {
    klog(LOG_ERROR, "DevFS: Failed to allocate /dev/console node.");
    // Cleanup if needed (skipping for brevity in this patch)
    return -ENOMEM;
  }

  strncpy(console_node->name, "console", MAX_FILENAME_LEN);
  console_node->flags = VFS_CHARDEVICE;
  console_node->inode = 2;
  console_node->length = 0;
  console_node->ptr = NULL;
  console_node->open = NULL;
  console_node->close = NULL;
  console_node->read = console_read;
  console_node->write = console_write; // We need to define this
  console_node->finddir = NULL;
  console_node->readdir = NULL;
  console_node->create = NULL;
  console_node->mkdir = NULL;
  console_node->remove = NULL;
  console_node->rmdir = NULL;
  console_node->stat = NULL;
  console_node->ioctl = NULL;

  global_console_node = console_node;
  klog(LOG_INFO, "DevFS: /dev/console node created.");

  klog(LOG_INFO, "DevFS: Mounted successfully.");
  return 0;
}

// --- devfs_init implementation ---
void devfs_init() {
  struct filesystem_type *devfs_fs_type =
      (struct filesystem_type *)kmalloc(sizeof(struct filesystem_type));
  if (!devfs_fs_type) {
    klog(LOG_ERROR, "DevFS: Failed to allocate filesystem_type for devfs.");
    return;
  }
  strncpy(devfs_fs_type->name, "devfs", 32); // Max 32 characters for fs name
  devfs_fs_type->mount_func = devfs_mount;
  devfs_fs_type->next = NULL;

  register_filesystem(devfs_fs_type);
  klog(LOG_INFO, "DevFS: Filesystem 'devfs' registered.");
}
