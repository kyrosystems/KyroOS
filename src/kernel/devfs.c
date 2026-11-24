#include "devfs.h"
#include "errno.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "random.h"
#include "vfs.h"
#include "keyboard.h"
#include "thread.h"

typedef struct devfs_entry {
  char name[MAX_FILENAME_LEN];
  vfs_node_t *node;
  struct devfs_entry *next;
} devfs_entry_t;

static devfs_entry_t *devfs_devices = NULL;
static vfs_node_t *devfs_root_node = NULL;

uint32_t console_write(vfs_node_t *node, uint64_t offset, uint32_t size,
                       uint8_t *buffer) {
  (void)node;
  (void)offset;
  if (!buffer || size == 0) return 0;
  char stack_buf[256];
  char *str = (size < sizeof(stack_buf)) ? stack_buf : (char *)kmalloc(size + 1);
  if (!str) return 0;
  memcpy(str, buffer, size);
  str[size] = '\0';
  klog_print_str(str, true);
  serial_print(str);
  if (str != stack_buf) kfree(str);
  return size;
}

uint32_t console_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                      uint8_t *buffer) {
  (void)node;
  (void)offset;
  if (size == 0 || !buffer) return 0;
  thread_t *t = get_current_thread();
  for (uint32_t i = 0; i < size; i++) {
    if (t && t->state == THREAD_DEAD) return i;
    buffer[i] = (uint8_t)keyboard_get_char_blocking();
    if (t && t->state == THREAD_DEAD) return i + 1;
  }
  return size;
}

uint32_t urandom_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                      uint8_t *buffer) {
  (void)node;
  (void)offset;
  if (!buffer || size == 0) return 0;
  random_get_bytes(buffer, size);
  return size;
}

static uint32_t null_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                          uint8_t *buffer) {
  (void)node; (void)offset; (void)size; (void)buffer;
  return 0; // EOF
}

static uint32_t null_write(vfs_node_t *node, uint64_t offset, uint32_t size,
                           uint8_t *buffer) {
  (void)node; (void)offset; (void)buffer;
  return size; // Discard
}

static uint32_t zero_read(vfs_node_t *node, uint64_t offset, uint32_t size,
                          uint8_t *buffer) {
  (void)node; (void)offset;
  if (buffer && size > 0) {
    memset(buffer, 0, size);
  }
  return size;
}

int devfs_register_device(const char *name, vfs_node_t *node) {
  if (!name || !node) return -EINVAL;
  devfs_entry_t *cur = devfs_devices;
  while (cur) {
    if (strcmp(cur->name, name) == 0) {
      cur->node = node;
      return 0;
    }
    cur = cur->next;
  }
  devfs_entry_t *entry = (devfs_entry_t *)kmalloc(sizeof(devfs_entry_t));
  if (!entry) return -ENOMEM;
  strncpy(entry->name, name, MAX_FILENAME_LEN - 1);
  entry->name[MAX_FILENAME_LEN - 1] = '\0';
  entry->node = node;
  entry->next = devfs_devices;
  devfs_devices = entry;
  klog(LOG_INFO, "DevFS: Registered /dev/%s", name);
  return 0;
}

vfs_node_t *devfs_finddir(vfs_node_t *node, char *name) {
  if (!name) return NULL;
  if (strcmp(name, ".") == 0) return node ? node : devfs_root_node;
  if (strcmp(name, "..") == 0) return vfs_root;
  devfs_entry_t *cur = devfs_devices;
  while (cur) {
    if (strcmp(cur->name, name) == 0) {
      return cur->node;
    }
    cur = cur->next;
  }
  return NULL;
}

int devfs_readdir(vfs_node_t *node, uint32_t index, struct dirent *dir_entry) {
  (void)node;
  if (!dir_entry) return -1;
  uint32_t count = 0;
  devfs_entry_t *cur = devfs_devices;
  while (cur) {
    count++;
    cur = cur->next;
  }
  if (index >= count) return 0;
  uint32_t target = count - 1 - index;
  cur = devfs_devices;
  for (uint32_t i = 0; i < target && cur; i++) {
    cur = cur->next;
  }
  if (cur) {
    strncpy(dir_entry->name, cur->name, MAX_FILENAME_LEN - 1);
    dir_entry->name[MAX_FILENAME_LEN - 1] = '\0';
    dir_entry->ino = cur->node ? cur->node->inode : (index + 1);
    return 1;
  }
  return 0;
}

static vfs_node_t *create_basic_node(const char *name, uint32_t flags,
                                     read_vfs_t read_fn, write_vfs_t write_fn) {
  vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  if (!node) return NULL;
  memset(node, 0, sizeof(vfs_node_t));
  strncpy(node->name, name, MAX_FILENAME_LEN - 1);
  node->flags = flags;
  node->inode = vfs_get_next_inode();
  node->read = read_fn;
  node->write = write_fn;
  node->mode = 0666;
  return node;
}

int devfs_mount(vfs_node_t *mount_point, vfs_node_t *device_node) {
  (void)device_node;
  if (!mount_point) {
    klog(LOG_ERROR, "DevFS: Mount point cannot be NULL.");
    return -EINVAL;
  }

  devfs_root_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
  if (!devfs_root_node) return -ENOMEM;
  memset(devfs_root_node, 0, sizeof(vfs_node_t));
  strncpy(devfs_root_node->name, "dev", MAX_FILENAME_LEN - 1);
  devfs_root_node->flags = VFS_DIRECTORY;
  devfs_root_node->inode = vfs_get_next_inode();
  devfs_root_node->finddir = devfs_finddir;
  devfs_root_node->readdir = devfs_readdir;
  devfs_root_node->mode = 0755;

  // Register built-in standard devices
  vfs_node_t *console_node = create_basic_node("console", VFS_CHARDEVICE, console_read, console_write);
  if (console_node) devfs_register_device("console", console_node);

  vfs_node_t *tty_node = create_basic_node("tty", VFS_CHARDEVICE, console_read, console_write);
  if (tty_node) devfs_register_device("tty", tty_node);

  vfs_node_t *urandom_node = create_basic_node("urandom", VFS_CHARDEVICE, urandom_read, NULL);
  if (urandom_node) devfs_register_device("urandom", urandom_node);

  vfs_node_t *null_node = create_basic_node("null", VFS_CHARDEVICE, null_read, null_write);
  if (null_node) devfs_register_device("null", null_node);

  vfs_node_t *zero_node = create_basic_node("zero", VFS_CHARDEVICE, zero_read, null_write);
  if (zero_node) devfs_register_device("zero", zero_node);

  // Copy ops to mount_point
  memcpy(mount_point->name, devfs_root_node->name, MAX_FILENAME_LEN);
  mount_point->flags = devfs_root_node->flags;
  mount_point->inode = devfs_root_node->inode;
  mount_point->length = 0;
  mount_point->ptr = NULL;
  mount_point->open = NULL;
  mount_point->close = NULL;
  mount_point->read = NULL;
  mount_point->write = NULL;
  mount_point->finddir = devfs_finddir;
  mount_point->readdir = devfs_readdir;
  mount_point->create = NULL;
  mount_point->mkdir = NULL;
  mount_point->remove = NULL;
  mount_point->rmdir = NULL;
  mount_point->stat = NULL;
  mount_point->ioctl = NULL;

  klog(LOG_INFO, "DevFS: Mounted at /dev successfully.");
  return 0;
}

void devfs_init() {
  struct filesystem_type *devfs_fs_type =
      (struct filesystem_type *)kmalloc(sizeof(struct filesystem_type));
  if (!devfs_fs_type) {
    klog(LOG_ERROR, "DevFS: Failed to allocate filesystem_type for devfs.");
    return;
  }
  strncpy(devfs_fs_type->name, "devfs", 32);
  devfs_fs_type->mount_func = devfs_mount;
  devfs_fs_type->next = NULL;

  register_filesystem(devfs_fs_type);
  klog(LOG_INFO, "DevFS: Filesystem 'devfs' registered.");
}
