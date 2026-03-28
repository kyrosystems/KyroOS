#include "syscall.h"
#include "elf.h"
#include "event.h"
#include "fb.h"
#include "heap.h"
#include "isr.h" // For timer_get_ticks
#include "kstring.h"
#include "log.h"
#include "pmm.h"
#include "scheduler.h"
#include "socket.h" // For socket_t, etc.
#include "thread.h"
#include "vfs.h"
#include "vmm.h"
#include "uaccess.h"

// Forward declarations from vmm.h
extern pml4_t *vmm_create_address_space(void);
extern void vmm_destroy_address_space(pml4_t *pml4);

// HHDM offset from kernel.c, needed for V_TO_P
extern uint64_t kernel_hhdm_offset;
#define V_TO_P(v) ((void *)((uint64_t)(v) - kernel_hhdm_offset))

typedef void (*syscall_func_t)(struct registers *regs);
static syscall_func_t syscall_table[256];

static void sys_exit(struct registers *regs) {
  (void)regs;
  thread_exit();
}

static void sys_write(struct registers *regs) {
  int fd = (int)regs->rdi;
  char *buffer = (char *)regs->rsi;
  size_t size = (size_t)regs->rdx;
  thread_t *t = get_current_thread();
  klog(LOG_DEBUG, "SYSCALL_ENTRY: sys_write: fd=%d, buffer=%p, size=%u, thread_id=%d", fd,
       buffer, size, t->id);

  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type == FD_TYPE_NONE) {
    klog(LOG_ERROR, "SYSCALL_EXIT: sys_write: Invalid FD %d or not open. Returning -1.", fd);
    regs->rax = (uint64_t)-1; // Invalid FD or not open
    return;
  }

  if (validate_user_pointer(buffer, size) != 0) {
      klog(LOG_ERROR, "SYSCALL_EXIT: sys_write: Invalid buffer pointer %p", buffer);
      regs->rax = (uint64_t)-1;
      return;
  }

  if (t->fd_table[fd].type == FD_TYPE_FILE) {
    uint64_t current_offset = t->fd_table[fd].data.file.offset;
    if (t->fd_table[fd].data.file.flags & O_APPEND) {
      current_offset =
          t->fd_table[fd].data.file.node->length; // For append, write at end
    }
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_write: Writing to file FD %d, offset %llu, size %u", fd, current_offset, size);
    regs->rax = vfs_write(t->fd_table[fd].data.file.node, current_offset, size,
                          (uint8_t *)buffer);
    if (regs->rax != (uint64_t)-1) {
      t->fd_table[fd].data.file.offset =
          current_offset + regs->rax; // Update offset
      klog(LOG_DEBUG, "SYSCALL_EXIT: sys_write: Wrote %llu bytes to FD %d. New offset %llu.", regs->rax, fd, t->fd_table[fd].data.file.offset);
    } else {
      klog(LOG_ERROR, "SYSCALL_EXIT: sys_write: vfs_write failed for FD %d. Returning -1.", fd);
    }
  } else if (t->fd_table[fd].type == FD_TYPE_SOCKET) {
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_write: Writing to socket FD %d, size %u", fd, size);
    regs->rax = sock_send(t->fd_table[fd].data.sock, buffer, size,
                          0);
    klog(LOG_DEBUG, "SYSCALL_EXIT: sys_write: Sent %llu bytes to socket FD %d.", regs->rax, fd);
  } else {
    klog(LOG_ERROR, "SYSCALL_EXIT: sys_write: Invalid FD type %d for FD %d. Returning -1.", t->fd_table[fd].type, fd);
    regs->rax = (uint64_t)-1; // Invalid FD type
  }
}

static void sys_open(struct registers *regs) {
  char *user_path = (char *)regs->rdi;
  int flags = (int)regs->rsi; // Get flags from rsi
  klog(LOG_DEBUG, "SYSCALL_ENTRY: sys_open: user_path=%p, flags=%x", user_path, flags);

  // Copy path from userspace to a kernel buffer to avoid faulting.
  char path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_path, 1) != 0) {
    klog(LOG_ERROR, "SYSCALL_EXIT: sys_open: Invalid user path pointer. Denying.");
    regs->rax = (uint64_t)-1;
    return;
  }
  strncpy(path, user_path, MAX_FILENAME_LEN - 1);
  path[MAX_FILENAME_LEN - 1] = '\0';
  klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_open: Resolved path: '%s'", path);

  vfs_node_t *node = vfs_resolve_path(vfs_root, path);

  // Handle O_CREAT flag
  if (!node && (flags & O_CREAT)) {
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_open: File '%s' not found, O_CREAT set. Attempting to create.", path);
    // Find parent directory
    char parent_path[MAX_FILENAME_LEN];
    char filename[MAX_FILENAME_LEN];
    char *last_slash = strrchr(path, '/');
    vfs_node_t *parent_node = vfs_root;

    if (last_slash) {
      int parent_len = last_slash - path;
      strncpy(parent_path, path, parent_len);
      parent_path[parent_len] = '\0';
      strncpy(filename, last_slash + 1, MAX_FILENAME_LEN);
      parent_path[MAX_FILENAME_LEN - 1] = '\0'; // Ensure null-termination
      filename[MAX_FILENAME_LEN - 1] = '\0'; // Ensure null-termination
      parent_node = vfs_resolve_path(vfs_root, parent_path);
    } else {
      // File in root, or current directory for relative path.
      strncpy(filename, path, MAX_FILENAME_LEN);
      filename[MAX_FILENAME_LEN - 1] = '\0'; // Ensure null-termination
    }

    if (!parent_node || !(parent_node->flags & VFS_DIRECTORY)) {
      regs->rax = (uint64_t)-1; // Parent not found or not a directory
      klog(LOG_ERROR,
           "SYSCALL_EXIT: sys_open: Parent directory not found or invalid for %s. Returning -1.",
           path);
      return;
    }
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_open: Creating file '%s' in parent '%s'", filename, parent_path);
    if (vfs_create(parent_node, filename, 0) != 0) {
      regs->rax = (uint64_t)-1;                                // Creation failed
      klog(LOG_ERROR, "SYSCALL_EXIT: sys_open: Failed to create file %s. Returning -1.", path);
      return;
    }
    node = vfs_resolve_path(
        vfs_root, path); // Resolve again to get the newly created node
    if (!node) {
      regs->rax = (uint64_t)-1; // Should not happen if create succeeded
      klog(LOG_ERROR,
           "SYSCALL_EXIT: sys_open: Created file %s but could not resolve it. Returning -1.",
           path);
      return;
    }
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_open: File '%s' created successfully.", path);
  } else if (!node) {
    regs->rax = (uint64_t)-1; // File not found and O_CREAT not set
    klog(LOG_WARN, "SYSCALL_EXIT: sys_open: File not found: %s. O_CREAT not set. Returning -1.", path);
    return;
  }

  // File exists or was created, now handle other flags
  if (node->open) {
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_open: Calling node->open for '%s' with flags %x", path, flags);
    node->open(node, flags);
  }

  thread_t *t = get_current_thread();
  for (int i = 0; i < MAX_FILES; i++) {
    if (t->fd_table[i].type == FD_TYPE_NONE) { // Check if slot is free
      t->fd_table[i].type = FD_TYPE_FILE;
      t->fd_table[i].data.file.node = node;
      t->fd_table[i].data.file.flags = flags;
      t->fd_table[i].data.file.offset =
          (flags & O_APPEND) ? node->length
                             : 0; // Set initial offset for append
      regs->rax = i;              // Return the file descriptor
      klog(LOG_DEBUG, "SYSCALL_EXIT: sys_open: Opened file '%s' successfully. FD=%llu.", path, regs->rax);
      return;
    }
  }
  regs->rax = (uint64_t)-1; // No free file descriptors
  klog(LOG_ERROR, "SYSCALL_EXIT: sys_open: No free file descriptors for %s. Returning -1.", path);
}

static void sys_close(struct registers *regs) {
  int fd = (int)regs->rdi;
  thread_t *t = get_current_thread();
  klog(LOG_DEBUG, "SYSCALL_ENTRY: sys_close: fd=%d, thread_id=%d", fd, t->id);

  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type == FD_TYPE_NONE) {
    klog(LOG_ERROR, "SYSCALL_EXIT: sys_close: Invalid FD %d or not open. Returning -1.", fd);
    regs->rax = (uint64_t)-1;
    return;
  }

  if (t->fd_table[fd].type == FD_TYPE_FILE) {
    vfs_node_t *node = t->fd_table[fd].data.file.node;
    if (node && node->close) {
        klog(LOG_DEBUG, "SYSCALL_DEBUG: Calling node->close for FD %d.", fd);
        node->close(node);
    }
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_close: Closing file FD %d.", fd);
    t->fd_table[fd].type = FD_TYPE_NONE;   // Clear type
    t->fd_table[fd].data.file.node = NULL; // Clear node, reset other fields
    t->fd_table[fd].data.file.offset = 0;
    t->fd_table[fd].data.file.flags = 0;
  } else if (t->fd_table[fd].type == FD_TYPE_SOCKET) {
    // Close socket
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_close: Closing socket FD %d.", fd);
    sock_close(t->fd_table[fd].data.sock);
    t->fd_table[fd].type = FD_TYPE_NONE; // Clear type
    t->fd_table[fd].data.sock = NULL;
  }
  regs->rax = 0;
  klog(LOG_DEBUG, "SYSCALL_EXIT: sys_close: Closed FD %d successfully. Returning 0.", fd);
}
static void sys_read(struct registers *regs) {
  int fd = (int)regs->rdi;
  char *buf = (char *)regs->rsi;
  size_t size = (size_t)regs->rdx;
  thread_t *t = get_current_thread();
  klog(LOG_DEBUG, "SYSCALL_ENTRY: sys_read: fd=%d, buf=%p, size=%u, thread_id=%d", fd,
       buf, size, t->id);

  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type == FD_TYPE_NONE) {
    klog(LOG_ERROR, "SYSCALL_EXIT: sys_read: Invalid FD %d or not open. Returning -1.", fd);
    regs->rax = (uint64_t)-1;
    return;
  }

  if (validate_user_pointer(buf, size) != 0) {
      klog(LOG_ERROR, "SYSCALL_EXIT: sys_read: Invalid buffer pointer %p", buf);
      regs->rax = (uint64_t)-1;
      return;
  }

  if (t->fd_table[fd].type == FD_TYPE_FILE) {
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_read: Reading from file FD %d, offset %llu, size %u", fd, t->fd_table[fd].data.file.offset, size);
    regs->rax =
        vfs_read(t->fd_table[fd].data.file.node,
                 t->fd_table[fd].data.file.offset, size, (uint8_t *)buf);
    if ((int64_t)regs->rax != -1) {
      t->fd_table[fd].data.file.offset += regs->rax; // Update offset
      klog(LOG_DEBUG, "SYSCALL_EXIT: sys_read: Read %llu bytes from FD %d. New offset %llu.", regs->rax, fd, t->fd_table[fd].data.file.offset);
    } else {
      klog(LOG_ERROR, "SYSCALL_EXIT: sys_read: vfs_read failed for FD %d. Returning -1.", fd);
    }
  } else if (t->fd_table[fd].type == FD_TYPE_SOCKET) {
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_read: Reading from socket FD %d, size %u", fd, size);
    regs->rax =
        sock_recv(t->fd_table[fd].data.sock, buf, size, 0);
    klog(LOG_DEBUG, "SYSCALL_EXIT: sys_read: Received %llu bytes from socket FD %d.", regs->rax, fd);
  } else {
    klog(LOG_ERROR, "SYSCALL_EXIT: sys_read: Invalid FD type %d for FD %d. Returning -1.", t->fd_table[fd].type, fd);
    regs->rax = (uint64_t)-1; // Invalid FD type
  }
}

static void sys_stat(struct registers *regs) {
  char *user_path = (char *)regs->rdi;
  struct stat *stat_buf = (struct stat *)regs->rsi;

  char path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_path, 1) != 0) {
    regs->rax = (uint64_t)-1;
    return;
  }
  strncpy(path, user_path, MAX_FILENAME_LEN - 1);
  path[MAX_FILENAME_LEN - 1] = '\0';

  if (validate_user_pointer(stat_buf, sizeof(struct stat)) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }


  vfs_node_t *node = vfs_resolve_path(vfs_root, path);
  if (!node) {
    regs->rax = -1; // Not found
    return;
  }

  if (node->stat) {
    regs->rax = node->stat(node, stat_buf);
  } else {
    regs->rax = -1; // Stat not implemented for this node
  }
}

static void sys_mkdir(struct registers *regs) {
  char *user_path = (char *)regs->rdi;

  char path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_path, 1) != 0) {
    regs->rax = (uint64_t)-1;
    return;
  }
  strncpy(path, user_path, MAX_FILENAME_LEN - 1);
  path[MAX_FILENAME_LEN - 1] = '\0';

  regs->rax = vfs_mkdir(vfs_root, path, 0);
}

static void sys_readdir(struct registers *regs) {
  vfs_node_t *node = (vfs_node_t *)regs->rdi;
  uint32_t index = (uint32_t)regs->rsi;
  struct dirent *dir_entry =
      (struct dirent *)regs->rdx; // Third argument for dirent buffer

  if (validate_user_pointer(dir_entry, sizeof(struct dirent)) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }

  if (!node) {
    node = vfs_root;
  }

  regs->rax = vfs_readdir(
      node, index,
      dir_entry); // vfs_readdir now returns int (1 for success, 0 for end)
}

static void sys_unlink(struct registers *regs) {
  char *user_path = (char *)regs->rdi;

  char path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_path, 1) != 0) {
    regs->rax = (uint64_t)-1;
    return;
  }
  strncpy(path, user_path, MAX_FILENAME_LEN - 1);
  path[MAX_FILENAME_LEN - 1] = '\0';

  regs->rax = vfs_remove(vfs_root, path);
}

static void sys_rmdir(struct registers *regs) {
  char *user_path = (char *)regs->rdi;

  char path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_path, 1) != 0) {
    regs->rax = (uint64_t)-1;
    return;
  }
  strncpy(path, user_path, MAX_FILENAME_LEN - 1);
  path[MAX_FILENAME_LEN - 1] = '\0';

  regs->rax = vfs_rmdir(vfs_root, path, 0);
}

static void sys_socket(struct registers *regs) {
  int domain = (int)regs->rdi;
  int type = (int)regs->rsi;
  int protocol = (int)regs->rdx;

  thread_t *t = get_current_thread();
  for (int i = 0; i < MAX_FILES; i++) { // Reuse MAX_FILES for sockets too
    if (t->fd_table[i].type == FD_TYPE_NONE) {
      socket_t *sock = sock_create(domain, type, protocol);
      if (sock) {
        t->fd_table[i].type = FD_TYPE_SOCKET;
        t->fd_table[i].data.sock = sock;
        regs->rax = i; // Return socket descriptor
        return;
      }
    }
  }
  regs->rax = -1; // No free descriptors
}

static void sys_connect(struct registers *regs) {
  int fd = (int)regs->rdi;
  sockaddr_in_t *addr = (sockaddr_in_t *)regs->rsi;
  // int addrlen = (int)regs->rdx; // Not used yet, assuming sockaddr_in_t size

  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type != FD_TYPE_SOCKET) {
    regs->rax = -1; // Not a valid socket FD
    return;
  }

  if (validate_user_pointer(addr, sizeof(sockaddr_in_t)) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }

  socket_t *sock = t->fd_table[fd].data.sock;
  regs->rax = sock_connect(sock, addr);
}

static void sys_bind(struct registers *regs) {
  int fd = (int)regs->rdi;
  sockaddr_in_t *addr = (sockaddr_in_t *)regs->rsi;
  // int addrlen = (int)regs->rdx; // Not used yet, assuming sockaddr_in_t size

  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type != FD_TYPE_SOCKET) {
    regs->rax = -1; // Not a valid socket FD
    return;
  }

  if (validate_user_pointer(addr, sizeof(sockaddr_in_t)) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }

  socket_t *sock = t->fd_table[fd].data.sock;
  regs->rax = sock_bind(sock, addr);
}

static void sys_listen(struct registers *regs) {
  int fd = (int)regs->rdi;
  int backlog = (int)regs->rsi;

  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type != FD_TYPE_SOCKET) {
    regs->rax = -1; // Not a valid socket FD
    return;
  }

  socket_t *sock = t->fd_table[fd].data.sock;
  regs->rax = sock_listen(sock, backlog);
}

static void sys_accept(struct registers *regs) {
  int fd = (int)regs->rdi;
  sockaddr_in_t *addr = (sockaddr_in_t *)regs->rsi;


  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type != FD_TYPE_SOCKET) {
    regs->rax = -1; // Not a valid socket FD
    return;
  }

  if (validate_user_pointer(addr, sizeof(sockaddr_in_t)) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }

  socket_t *new_sock = sock_accept(t->fd_table[fd].data.sock, addr);
  if (!new_sock) {
    regs->rax = -1; // Accept failed
    return;
  }

  for (int i = 0; i < MAX_FILES; i++) {
    if (t->fd_table[i].type == FD_TYPE_NONE) { // Check if slot is free
      t->fd_table[i].type = FD_TYPE_SOCKET;
      t->fd_table[i].data.sock = new_sock;
      regs->rax = i; // Return the new socket descriptor
      return;
    }
  }
  regs->rax = -1; // No free descriptors
}

#include "crypto.h" // For sha256_hash

// ... (existing syscalls)

static void sys_recv(struct registers *regs) {
  int fd = (int)regs->rdi;
  void *buf = (void *)regs->rsi;
  size_t len = (size_t)regs->rdx;
  int flags = (int)regs->r10; // Assuming r10 for 4th arg

  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type != FD_TYPE_SOCKET) {
    regs->rax = -1; // Not a valid socket FD
    return;
  }

  if (validate_user_pointer(buf, len) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }

  socket_t *sock = t->fd_table[fd].data.sock;
  regs->rax = sock_recv(sock, buf, len, flags);
}

static void sys_sha256(struct registers *regs) {
  const uint8_t *data = (const uint8_t *)regs->rdi;
  size_t len = (size_t)regs->rsi;
  uint8_t *hash_out = (uint8_t *)regs->rdx;

  if (validate_user_pointer(data, len) != 0 || validate_user_pointer(hash_out, 32) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }

  sha256_hash(data, len, hash_out);
  regs->rax = 0; // Success
}

static void sys_malloc(struct registers *regs) {
  size_t size = (size_t)regs->rdi;
  regs->rax = (uint64_t)kmalloc(size);
}

static void sys_free(struct registers *regs) {
  void *ptr = (void *)regs->rdi;
  kfree(ptr);
  regs->rax = 0; // Success
}

static void sys_get_ticks(struct registers *regs) {
  regs->rax = timer_get_ticks();
}

static void sys_ioctl(struct registers *regs) {
  int fd = (int)regs->rdi;
  int request = (int)regs->rsi;
  void *argp = (void *)regs->rdx;

  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type == FD_TYPE_NONE) {
    regs->rax = -1; // Invalid FD
    return;
  }


  if (t->fd_table[fd].type == FD_TYPE_FILE &&
      t->fd_table[fd].data.file.node->ioctl) {
    regs->rax = t->fd_table[fd].data.file.node->ioctl(
        t->fd_table[fd].data.file.node, request, argp);
  } else {
    regs->rax = -1; // IOCTL not supported for this FD type or node.
  }
}

// New Mount/Unmount Syscalls
static void sys_mount(struct registers *regs) {
  char *user_mount_point_path = (char *)regs->rdi;
  char *user_device_node_path = (char *)regs->rsi;
  char *user_fs_type_name = (char *)regs->rdx;

  char mount_point_path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_mount_point_path, 1) != 0) {
    regs->rax = -1;
    return;
  }
  strncpy(mount_point_path, user_mount_point_path, MAX_FILENAME_LEN - 1);
  mount_point_path[MAX_FILENAME_LEN - 1] = '\0';

  char device_node_path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_device_node_path, 1) != 0) {
    regs->rax = -1;
    return;
  }
  strncpy(device_node_path, user_device_node_path, MAX_FILENAME_LEN - 1);
  device_node_path[MAX_FILENAME_LEN - 1] = '\0';

  char fs_type_name[32]; // Filesystem names are short
  if (validate_user_pointer(user_fs_type_name, 1) != 0) {
    regs->rax = -1;
    return;
  }
  strncpy(fs_type_name, user_fs_type_name, 31);
  fs_type_name[31] = '\0';

  vfs_node_t *mount_point = vfs_resolve_path(vfs_root, mount_point_path);
  if (!mount_point || !(mount_point->flags & VFS_DIRECTORY)) {
    klog(LOG_ERROR, "SYSCALL: sys_mount: Invalid mount point %s.",
         mount_point_path);
    regs->rax = -1;
    return;
  }

  vfs_node_t *device_node = vfs_resolve_path(vfs_root, device_node_path);
  if (!device_node ||
      !(device_node->flags & VFS_FILE)) { // Must be a block device file
    klog(LOG_ERROR, "SYSCALL: sys_mount: Invalid device node %s.",
         device_node_path);
    regs->rax = -1;
    return;
  }

  regs->rax = vfs_mount(mount_point, device_node, fs_type_name);
}

static void sys_unmount(struct registers *regs) {
  char *user_mount_point_path = (char *)regs->rdi;

  char mount_point_path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_mount_point_path, 1) != 0) {
    regs->rax = -1;
    return;
  }
  strncpy(mount_point_path, user_mount_point_path, MAX_FILENAME_LEN - 1);
  mount_point_path[MAX_FILENAME_LEN - 1] = '\0';

  vfs_node_t *mount_point = vfs_resolve_path(vfs_root, mount_point_path);
  if (!mount_point || !(mount_point->flags & VFS_MOUNTPOINT)) {
    klog(LOG_ERROR, "SYSCALL: sys_unmount: Not a mount point %s.",
         mount_point_path);
    regs->rax = -1;
    return;
  }

  regs->rax = vfs_unmount(mount_point);
}

static void sys_exec(struct registers *regs) {
    char *user_path = (char *)regs->rdi;
    klog(LOG_DEBUG, "SYSCALL_EXEC: path=%p", user_path);
    
    if (validate_user_pointer(user_path, 1) != 0) {
        regs->rax = (uint64_t)-1;
        return;
    }
    
    char path[MAX_FILENAME_LEN];
    strncpy(path, user_path, MAX_FILENAME_LEN - 1);
    path[MAX_FILENAME_LEN - 1] = '\0';
    
    vfs_node_t *node = vfs_resolve_path(vfs_root, path);
    if (!node || (node->flags & VFS_DIRECTORY)) {
        klog(LOG_ERROR, "SYSCALL_EXEC: File not found or is directory: %s", path);
        regs->rax = (uint64_t)-1;
        return;
    }
    
    // Read ELF header
    uint8_t elf_header[64];
    if (vfs_read(node, 0, 64, elf_header) != 64) {
        klog(LOG_ERROR, "SYSCALL_EXEC: Failed to read ELF header");
        regs->rax = (uint64_t)-1;
        return;
    }
    
    // Verify ELF magic
    if (elf_header[0] != 0x7F || elf_header[1] != 'E' || 
        elf_header[2] != 'L' || elf_header[3] != 'F') {
        klog(LOG_ERROR, "SYSCALL_EXEC: Not a valid ELF file");
        regs->rax = (uint64_t)-1;
        return;
    }
    
    // Load entire file into memory
    uint8_t *elf_data = (uint8_t *)kmalloc(node->length);
    if (!elf_data) {
        klog(LOG_ERROR, "SYSCALL_EXEC: Failed to allocate memory for ELF");
        regs->rax = (uint64_t)-1;
        return;
    }
    
    if (vfs_read(node, 0, node->length, elf_data) != node->length) {
        klog(LOG_ERROR, "SYSCALL_EXEC: Failed to read ELF file");
        kfree(elf_data);
        regs->rax = (uint64_t)-1;
        return;
    }
    
    // Parse ELF and create new address space
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_data;
    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf_data + ehdr->e_phoff);
    
    // Allocate new PML4 for the process
    pml4_t *new_pml4 = vmm_create_address_space();
    if (!new_pml4) {
        klog(LOG_ERROR, "SYSCALL_EXEC: Failed to create new PML4");
        kfree(elf_data);
        regs->rax = (uint64_t)-1;
        return;
    }
    
    // Load program headers
    uint64_t entry_point = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            // Allocate pages for this segment
            uint64_t vaddr = phdr[i].p_vaddr & ~0xFFF;
            uint64_t end_vaddr = (phdr[i].p_vaddr + phdr[i].p_memsz + 0xFFF) & ~0xFFF;
            
            for (uint64_t addr = vaddr; addr < end_vaddr; addr += PAGE_SIZE) {
                void *phys = pmm_alloc_page();
                if (!phys) {
                    klog(LOG_ERROR, "SYSCALL_EXEC: Failed to allocate page");
                    vmm_destroy_address_space(new_pml4);
                    kfree(elf_data);
                    regs->rax = (uint64_t)-1;
                    return;
                }
                memset(phys, 0, PAGE_SIZE);
                vmm_map_page(new_pml4, (void *)(addr + kernel_hhdm_offset), phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
            }
            
            // Copy data to the mapped pages
            uint8_t *vaddr_ptr = (uint8_t *)(phdr[i].p_vaddr + kernel_hhdm_offset);
            memcpy(vaddr_ptr, elf_data + phdr[i].p_offset, phdr[i].p_filesz);
            
            if (i == 0) entry_point = ehdr->e_entry;
        }
    }
    
    // Allocate user stack
    uint64_t stack_top = 0x8000000000; // User stack address
    for (uint64_t addr = stack_top - (8 * PAGE_SIZE); addr < stack_top; addr += PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            klog(LOG_ERROR, "SYSCALL_EXEC: Failed to allocate stack page");
            vmm_destroy_address_space(new_pml4);
            kfree(elf_data);
            regs->rax = (uint64_t)-1;
            return;
        }
        memset(phys, 0, PAGE_SIZE);
        vmm_map_page(new_pml4, (void *)(addr + kernel_hhdm_offset), phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
    
    kfree(elf_data);
    
    // Use existing thread_create_userspace function
    thread_t *new_thread = thread_create_userspace(entry_point, new_pml4, 0, 0, NULL);
    if (!new_thread) {
        klog(LOG_ERROR, "SYSCALL_EXEC: Failed to create thread");
        vmm_destroy_address_space(new_pml4);
        regs->rax = (uint64_t)-1;
        return;
    }
    
    klog(LOG_INFO, "SYSCALL_EXEC: Created userspace thread at entry %p", (void *)entry_point);
    regs->rax = 0; // Success
}

static void sys_gfx_get_fb_info(struct registers *regs) {
// This is the virtual address where the framebuffer will be mapped in every
// userspace process that requests it.
#define USER_FRAMEBUFFER_VADDR 0x10000000000

  // This struct layout MUST MATCH the one in userspace/lib/tui/tui.h
  typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    void *buffer;
  } user_fb_info_t;

  user_fb_info_t *user_info_ptr = (user_fb_info_t *)regs->rdi;
  klog(LOG_DEBUG, "SYSCALL_GFX: user_info_ptr = %p", user_info_ptr);

  if (validate_user_pointer(user_info_ptr, sizeof(user_fb_info_t)) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }

  const fb_info_t *kernel_fb =
      fb_get_info(); // fb_info_t is limine_framebuffer here

  if (kernel_fb && kernel_fb->address) {
    klog(LOG_DEBUG,
         "SYSCALL_GFX: kernel_fb->address = %p, kernel_fb->pitch = %u, "
         "kernel_fb->height = %u",
         kernel_fb->address, kernel_fb->pitch, kernel_fb->height);

    void *fb_phys_addr = V_TO_P(kernel_fb->address);
    uint64_t fb_size = (uint64_t)kernel_fb->pitch * kernel_fb->height;
    klog(LOG_DEBUG, "SYSCALL_GFX: fb_phys_addr = %p, fb_size = %llu",
         fb_phys_addr, fb_size);

    for (uint64_t i = 0; i < fb_size; i += PAGE_SIZE) {
      void *phys = (void *)((uint64_t)fb_phys_addr + i);
      void *virt = (void *)(USER_FRAMEBUFFER_VADDR + i);
      klog(LOG_DEBUG, "SYSCALL_GFX: Mapping phys %p to virt %p in user PML4 %p",
           phys, virt, get_current_thread()->pml4);
      vmm_map_page(get_current_thread()->pml4, virt, phys,
                   PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }

    // Second, create the info struct to return to the user.
    user_fb_info_t temp_info;
    temp_info.width = kernel_fb->width;
    temp_info.height = kernel_fb->height;
    temp_info.pitch = kernel_fb->pitch;
    temp_info.bpp = kernel_fb->bpp;
    // IMPORTANT: Give the user the NEW virtual address, not the kernel's.
    temp_info.buffer = (void *)USER_FRAMEBUFFER_VADDR;

    klog(LOG_DEBUG,
         "SYSCALL_GFX: Copying temp_info from %p to user_info_ptr %p (size %u)",
         &temp_info, user_info_ptr, sizeof(user_fb_info_t));
    // Safely copy the correctly-sized structure to the userspace pointer.
    memcpy(user_info_ptr, &temp_info, sizeof(user_fb_info_t));

    regs->rax = 0; // Success
  } else {
    klog(LOG_ERROR,
         "SYSCALL_GFX: kernel_fb is NULL or kernel_fb->address is NULL.");
    regs->rax = -1; // Failure
  }
}

static void sys_input_poll_event(struct registers *regs) {
  event_t *user_event = (event_t *)regs->rdi;
  if (validate_user_pointer(user_event, sizeof(event_t)) != 0) {
      regs->rax = 0;
      return;
  }

  if (event_pop(user_event))
    regs->rax = 1;
  else
    regs->rax = 0;
}

static void sys_sbrk(struct registers *regs) {
  intptr_t increment = (intptr_t)regs->rdi;
  thread_t *t = get_current_thread();

  uint64_t old_brk = t->program_break;
  uint64_t new_brk = old_brk + increment;

  if (increment == 0) {
    regs->rax = old_brk;
    return;
  }

  if (new_brk < t->initial_program_break) {
    regs->rax = (uint64_t)-1;
    return;
  }

  if (new_brk > old_brk) {
      // Allocate and map pages
      for (uint64_t addr = (old_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); addr < new_brk; addr += PAGE_SIZE) {
          void *phys = pmm_alloc_page();
          if (!phys) {
              regs->rax = (uint64_t)-1;
              return;
          }
          vmm_map_page(t->pml4, (void*)addr, phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
      }
  } else if (new_brk < old_brk) {
      // Unmap and free pages
      for (uint64_t addr = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); addr < old_brk; addr += PAGE_SIZE) {
          void *phys = vmm_unmap_page(t->pml4, (void*)addr);
          if (phys) {
              pmm_free_page(phys);
          }
      }
  }

  t->program_break = new_brk;
  regs->rax = old_brk;
}

void syscall_init() {
  memset(syscall_table, 0, sizeof(syscall_table));
  syscall_table[SYS_EXIT] = sys_exit;
  syscall_table[SYS_WRITE] = sys_write;
  syscall_table[SYS_OPEN] = sys_open;
  syscall_table[SYS_CLOSE] = sys_close;
  syscall_table[SYS_READ] = sys_read;
  syscall_table[SYS_STAT] = sys_stat; // New syscall
  syscall_table[SYS_MKDIR] = sys_mkdir;
  syscall_table[SYS_READDIR] = sys_readdir;
  syscall_table[SYS_UNLINK] = sys_unlink;
  syscall_table[SYS_RMDIR] = sys_rmdir; // New syscall
  // syscall_table[SYS_CREATE] = sys_create; // Removed, functionality in
  // sys_open
  syscall_table[SYS_SOCKET] = sys_socket;
  syscall_table[SYS_CONNECT] = sys_connect;
  syscall_table[SYS_SEND] = sys_write; // Reuse sys_write for socket send
  syscall_table[SYS_RECV] = sys_recv;
  syscall_table[SYS_BIND] = sys_bind;
  syscall_table[SYS_LISTEN] = sys_listen;
  syscall_table[SYS_ACCEPT] = sys_accept;
  syscall_table[SYS_GET_TICKS] = sys_get_ticks;
  syscall_table[SYS_SHA256] = sys_sha256;
  syscall_table[SYS_MALLOC] = sys_malloc;
  syscall_table[SYS_FREE] = sys_free;
  syscall_table[SYS_IOCTL] = sys_ioctl;
  syscall_table[SYS_MOUNT] = sys_mount;
  syscall_table[SYS_UNMOUNT] = sys_unmount;
  syscall_table[SYS_EXEC] = sys_exec;
  syscall_table[SYS_GFX_GET_FB_INFO] = sys_gfx_get_fb_info;
  syscall_table[SYS_INPUT_POLL_EVENT] = sys_input_poll_event;
  syscall_table[SYS_SBRK] = sys_sbrk;
  klog(LOG_INFO, "Syscall handler expanded.");
}

void syscall_handler(struct registers *regs) {
  uint64_t syscall_num = regs->rax;
  klog(LOG_INFO, "SYSCALL: #%llu (rax=%llx, rdi=%llx, rsi=%llx, rdx=%llx)",
       syscall_num, regs->rax, regs->rdi, regs->rsi, regs->rdx);
  if (syscall_num < 256 && syscall_table[syscall_num] != 0) {
    syscall_table[syscall_num](regs);
  } else {
    klog(LOG_WARN, "Unknown syscall received: %llu", syscall_num);
  }
}
