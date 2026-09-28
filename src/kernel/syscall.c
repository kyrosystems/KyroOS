#include "syscall.h"
#include "elf.h"
#include "errno.h"
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

static vfs_node_t *resolve_for_thread(const char *path) {
  thread_t *t = get_current_thread();
  const char *p = path;
  if (t && path[0] != '/' && path[0] != '\0') {
    static char absbuf[MAX_FILENAME_LEN];
    const char *cwd = t->cwd[0] ? t->cwd : "/";
    ksprintf(absbuf, "%s%s%s", cwd,
             (cwd[strlen(cwd) - 1] == '/') ? "" : "/", path);
    p = absbuf;
  }
  return vfs_resolve_path(vfs_root, p);
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

  vfs_node_t *node = resolve_for_thread(path);

  // Handle O_CREAT flag
  if (!node && (flags & O_CREAT)) {
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_open: File '%s' not found, O_CREAT set. Attempting to create.", path);
    // Find parent directory
    char parent_path[MAX_FILENAME_LEN];
    char filename[MAX_FILENAME_LEN];
    char *last_slash = strrchr(path, '/');
    vfs_node_t *parent_node;

    if (last_slash && last_slash != path) {
      int parent_len = last_slash - path;
      strncpy(parent_path, path, parent_len);
      parent_path[parent_len] = '\0';
      strncpy(filename, last_slash + 1, MAX_FILENAME_LEN);
      parent_path[MAX_FILENAME_LEN - 1] = '\0'; // Ensure null-termination
      filename[MAX_FILENAME_LEN - 1] = '\0'; // Ensure null-termination
      parent_node = resolve_for_thread(parent_path);
    } else {
      // File in root, or current directory for relative path.
      if (last_slash == path) {
        strncpy(filename, path + 1, MAX_FILENAME_LEN); // "/name"
        filename[MAX_FILENAME_LEN - 1] = '\0';
        parent_node = vfs_root;
      } else {
        strncpy(filename, path, MAX_FILENAME_LEN);
        filename[MAX_FILENAME_LEN - 1] = '\0';
        // Relative to the thread's cwd node
        thread_t *tc = get_current_thread();
        parent_node = (tc && tc->cwd[0] && strcmp(tc->cwd, "/") != 0)
                          ? resolve_for_thread(tc->cwd) : vfs_root;
        if (!parent_node) parent_node = vfs_root;
      }
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
    // Ownership + umask for the new node (POSIX-style)
    thread_t *tc = get_current_thread();
    vfs_node_set_owner(node, tc->euid, tc->egid,
                       (uint16_t)vfs_default_mode_for(node->flags, tc));
  } else if (!node) {
    regs->rax = (uint64_t)-1; // File not found and O_CREAT not set
    klog(LOG_WARN, "SYSCALL_EXIT: sys_open: File not found: %s. O_CREAT not set. Returning -1.", path);
    return;
  }

  // File exists or was created, now handle other flags
  // Permission check for the requested access mode
  {
    int amode = (flags & O_WRONLY) ? 2 : ((flags & O_RDWR) ? 2 : 4);
    if (flags & O_RDWR) amode = 2 | 4;
    int ac = vfs_check_access(node, amode);
    if (ac != 0) { regs->rax = (uint64_t)ac; return; }
  }
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
  /* ABI: small values are fds; large values are legacy raw node pointers. */
  vfs_node_t *node = NULL;
  uint64_t rd = regs->rdi;
  if (rd < MAX_FILES) {
    thread_t *t = get_current_thread();
    if (!t || t->fd_table[rd].type != FD_TYPE_FILE ||
        !t->fd_table[rd].data.file.node) { regs->rax = -EBADF; return; }
    node = t->fd_table[rd].data.file.node;
    if (!(node->flags & VFS_DIRECTORY)) { regs->rax = -ENOTDIR; return; }
  } else {
    node = (vfs_node_t *)rd;
  }
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

  thread_t *tc = get_current_thread();
  if (!tc || tc->euid != 0) { regs->rax = -EPERM; return; } // registry: root

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
    
    // Derive process name from basename of the path
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    // Use existing thread_create_userspace function
    thread_t *new_thread = thread_create_userspace(entry_point, new_pml4, 0, 0, NULL, base);
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

// ---------------------------------------------------------------------
// Syscalls 28-59 (see abi/syscalls.def). Error contract: return
// (uint64_t)-errno; 0/non-negative on success.
// ---------------------------------------------------------------------

#include "epstein.h"
#include "procinfo.h"
#include "dns.h"
#include "errno.h"
#include "ip.h"
#include "net.h"

// Find the socket bound to fd. Returns 0 on success, -errno on failure.
static int fd_get_socket(thread_t *t, int fd, socket_t **out) {
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type != FD_TYPE_SOCKET)
    return -ENOTSOCK;
  *out = t->fd_table[fd].data.sock;
  if (!*out) return -EBADF;
  return 0;
}

// Copy a NUL-terminated path from userspace. Returns 0 or -errno.
static int copy_user_path(const char *user_path, char *out, size_t outsz) {
  if (validate_user_pointer(user_path, 1) != 0) return -EFAULT;
  size_t i = 0;
  while (i + 1 < outsz) {
    char c;
    // Copy byte-by-byte; the first page may be the only valid one.
    if (validate_user_pointer(user_path + i, 1) != 0) {
      if (i == 0) return -EFAULT;
      break; // ran off the end of the string into unmapped memory
    }
    c = user_path[i];
    out[i] = c;
    if (c == '\0') return 0;
    i++;
  }
  out[outsz - 1] = '\0';
  return -ENAMETOOLONG;
}

// Resolve a user path against the thread cwd. Absolute paths pass through.

static void sys_sendto(struct registers *regs) {
  int fd = (int)regs->rdi;
  void *buf = (void *)regs->rsi;
  size_t len = (size_t)regs->rdx;
  int flags = (int)regs->r10;
  sockaddr_in_t *addr = (sockaddr_in_t *)regs->r8;
  socklen_t addrlen = (socklen_t)regs->r9;

  thread_t *t = get_current_thread();
  socket_t *sock;
  int rc = fd_get_socket(t, fd, &sock);
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  if (validate_user_pointer(buf, len) != 0) { regs->rax = -EFAULT; return; }
  if (addr) {
    if (addrlen < sizeof(sockaddr_in_t) ||
        validate_user_pointer(addr, sizeof(sockaddr_in_t)) != 0) {
      regs->rax = -EFAULT;
      return;
    }
    if (addr->sin_family != AF_INET) { regs->rax = -EAFNOSUPPORT; return; }
  }
  regs->rax = (uint64_t)sock_sendto(sock, buf, len, flags, addr);
}

static void sys_recvfrom(struct registers *regs) {
  int fd = (int)regs->rdi;
  void *buf = (void *)regs->rsi;
  size_t len = (size_t)regs->rdx;
  int flags = (int)regs->r10;
  sockaddr_in_t *addr = (sockaddr_in_t *)regs->r8;
  socklen_t *addrlen = (socklen_t *)regs->r9;

  thread_t *t = get_current_thread();
  socket_t *sock;
  int rc = fd_get_socket(t, fd, &sock);
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  if (validate_user_pointer(buf, len) != 0) { regs->rax = -EFAULT; return; }
  if (addr && validate_user_pointer(addr, sizeof(sockaddr_in_t)) != 0) {
    regs->rax = -EFAULT;
    return;
  }
  if (addrlen && validate_user_pointer(addrlen, sizeof(socklen_t)) != 0) {
    regs->rax = -EFAULT;
    return;
  }
  regs->rax = (uint64_t)sock_recvfrom(sock, buf, len, flags, addr, addrlen);
}

static void sys_setsockopt(struct registers *regs) {
  int fd = (int)regs->rdi;
  int level = (int)regs->rsi;
  int optname = (int)regs->rdx;
  const void *optval = (const void *)regs->r10;
  socklen_t optlen = (socklen_t)regs->r8;

  thread_t *t = get_current_thread();
  socket_t *sock;
  int rc = fd_get_socket(t, fd, &sock);
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  if (optval && validate_user_pointer(optval, optlen) != 0) {
    regs->rax = -EFAULT;
    return;
  }
  regs->rax = (uint64_t)sock_setsockopt(sock, level, optname, optval, optlen);
}

static void sys_dns_resolve(struct registers *regs) {
  const char *host = (const char *)regs->rdi;
  char name[256];
  int rc = copy_user_path(host, name, sizeof(name));
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  uint32_t ip = dns_resolve(name);
  regs->rax = ip; // 0 = resolution failure (documented ABI)
}

static void sys_debugmode_get(struct registers *regs) {
  regs->rax = (uint64_t)(epstein ? 1 : 0);
}

static void sys_debugmode_set(struct registers *regs) {
  int mode = (int)regs->rdi;
  thread_t *t = get_current_thread();
  if (!t || t->euid != 0) { regs->rax = -EPERM; return; }
  epstein = mode ? 1 : 0; // atomic enough: single aligned store, IRQ-safe
  klog(LOG_INFO, "debugmode: set to %d by uid %u", epstein, t->euid);
  regs->rax = 0;
}

static void sys_getuid(struct registers *regs) {
  thread_t *t = get_current_thread();
  regs->rax = t ? t->uid : 0;
}

static void sys_geteuid(struct registers *regs) {
  thread_t *t = get_current_thread();
  regs->rax = t ? t->euid : 0;
}

static void sys_getgid(struct registers *regs) {
  thread_t *t = get_current_thread();
  regs->rax = t ? t->gid : 0;
}

static void sys_getegid(struct registers *regs) {
  thread_t *t = get_current_thread();
  regs->rax = t ? t->egid : 0;
}

static void sys_umask(struct registers *regs) {
  uint32_t mask = (uint32_t)regs->rdi & 0777;
  thread_t *t = get_current_thread();
  uint32_t old = t ? t->umask : 022;
  if (t) t->umask = mask;
  regs->rax = old;
}

static void sys_chmod(struct registers *regs) {
  const char *user_path = (const char *)regs->rdi;
  uint32_t mode = (uint32_t)regs->rsi & 0777;
  char path[MAX_FILENAME_LEN];
  int rc = copy_user_path(user_path, path, sizeof(path));
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  vfs_node_t *node = resolve_for_thread(path);
  if (!node) { regs->rax = -ENOENT; return; }
  thread_t *t = get_current_thread();
  if (t->euid != 0 && t->euid != node->uid) { regs->rax = -EPERM; return; }
  node->mode = mode;
  regs->rax = 0;
}

static void sys_fchmod(struct registers *regs) {
  int fd = (int)regs->rdi;
  uint32_t mode = (uint32_t)regs->rsi & 0777;
  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type != FD_TYPE_FILE) {
    regs->rax = -EBADF;
    return;
  }
  vfs_node_t *node = t->fd_table[fd].data.file.node;
  if (!node) { regs->rax = -EBADF; return; }
  if (t->euid != 0 && t->euid != node->uid) { regs->rax = -EPERM; return; }
  node->mode = mode;
  regs->rax = 0;
}

static void sys_chown(struct registers *regs) {
  const char *user_path = (const char *)regs->rdi;
  uint32_t uid = (uint32_t)regs->rsi;
  uint32_t gid = (uint32_t)regs->rdx;
  char path[MAX_FILENAME_LEN];
  int rc = copy_user_path(user_path, path, sizeof(path));
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  thread_t *t = get_current_thread();
  if (t->euid != 0) { regs->rax = -EPERM; return; } // no setuid binaries yet
  vfs_node_t *node = resolve_for_thread(path);
  if (!node) { regs->rax = -ENOENT; return; }
  node->uid = uid;
  node->gid = gid;
  regs->rax = 0;
}

static void sys_fchown(struct registers *regs) {
  int fd = (int)regs->rdi;
  uint32_t uid = (uint32_t)regs->rsi;
  uint32_t gid = (uint32_t)regs->rdx;
  thread_t *t = get_current_thread();
  if (t->euid != 0) { regs->rax = -EPERM; return; }
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type != FD_TYPE_FILE) {
    regs->rax = -EBADF;
    return;
  }
  vfs_node_t *node = t->fd_table[fd].data.file.node;
  if (!node) { regs->rax = -EBADF; return; }
  node->uid = uid;
  node->gid = gid;
  regs->rax = 0;
}

static void sys_setuid(struct registers *regs) {
  uint32_t uid = (uint32_t)regs->rdi;
  thread_t *t = get_current_thread();
  if (t->euid != 0) { regs->rax = -EPERM; return; }
  t->uid = t->euid = uid;
  regs->rax = 0;
}

static void sys_setgid(struct registers *regs) {
  uint32_t gid = (uint32_t)regs->rdi;
  thread_t *t = get_current_thread();
  if (t->euid != 0) { regs->rax = -EPERM; return; }
  t->gid = t->egid = gid;
  regs->rax = 0;
}

static void sys_seteuid(struct registers *regs) {
  uint32_t euid = (uint32_t)regs->rdi;
  thread_t *t = get_current_thread();
  // Simplified saved-credentials model: root may switch to anything,
  // others only back to their real uid.
  if (t->euid != 0 && euid != t->uid) { regs->rax = -EPERM; return; }
  t->euid = euid;
  regs->rax = 0;
}

static void sys_setegid(struct registers *regs) {
  uint32_t egid = (uint32_t)regs->rdi;
  thread_t *t = get_current_thread();
  if (t->egid != 0 && egid != t->gid) { regs->rax = -EPERM; return; }
  t->egid = egid;
  regs->rax = 0;
}

static void sys_getcwd(struct registers *regs) {
  char *buf = (char *)regs->rdi;
  size_t size = (size_t)regs->rsi;
  thread_t *t = get_current_thread();
  if (validate_user_pointer(buf, size) != 0) { regs->rax = -EFAULT; return; }
  const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
  size_t len = strlen(cwd) + 1;
  if (size < len) { regs->rax = -EINVAL; return; }
  memcpy(buf, cwd, len);
  regs->rax = 0;
}

static void sys_chdir(struct registers *regs) {
  const char *user_path = (const char *)regs->rdi;
  char path[MAX_FILENAME_LEN];
  int rc = copy_user_path(user_path, path, sizeof(path));
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  vfs_node_t *node = resolve_for_thread(path);
  if (!node) { regs->rax = -ENOENT; return; }
  if (!(node->flags & VFS_DIRECTORY)) { regs->rax = -ENOTDIR; return; }
  rc = vfs_check_access(node, 1); // need search permission on the directory
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  thread_t *t = get_current_thread();
  if (path[0] == '/') {
    strncpy(t->cwd, path, sizeof(t->cwd) - 1);
  } else {
    const char *cwd = t->cwd[0] ? t->cwd : "/";
    ksprintf(t->cwd, "%s%s%s", cwd,
             (cwd[strlen(cwd) - 1] == '/') ? "" : "/", path);
  }
  t->cwd[sizeof(t->cwd) - 1] = '\0';
  regs->rax = 0;
}

static void sys_getpid(struct registers *regs) {
  thread_t *t = get_current_thread();
  regs->rax = t ? t->id : 0;
}

static void sys_kill(struct registers *regs) {
  uint32_t pid = (uint32_t)regs->rdi;
  int sig = (int)regs->rsi;
  thread_t *t = get_current_thread();
  if (sig != 9 && sig != 15) { regs->rax = -EINVAL; return; } // SIGKILL/SIGTERM only
  if (pid == t->id) { thread_exit(); return; } // does not return

  // Walk the scheduler ring exactly once.
  thread_t *target = NULL;
  thread_t *start = t->next;
  if (start) {
    thread_t *cur = start;
    do {
      if (cur->id == pid && cur->state != THREAD_DEAD) { target = cur; break; }
      cur = cur->next;
    } while (cur != start);
  }
  if (!target) { regs->rax = -ESRCH; return; }
  if (t->euid != 0 && t->euid != target->euid) { regs->rax = -EPERM; return; }
  target->state = THREAD_DEAD;
  regs->rax = 0;
}

static void sys_dup(struct registers *regs) {
  int fd = (int)regs->rdi;
  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type == FD_TYPE_NONE) {
    regs->rax = -EBADF;
    return;
  }
  for (int i = 0; i < MAX_FILES; i++) {
    if (t->fd_table[i].type == FD_TYPE_NONE) {
      t->fd_table[i] = t->fd_table[fd];
      regs->rax = i;
      return;
    }
  }
  regs->rax = -EMFILE;
}

static void sys_dup2(struct registers *regs) {
  int fd = (int)regs->rdi;
  int fd2 = (int)regs->rsi;
  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || fd2 < 0 || fd2 >= MAX_FILES ||
      t->fd_table[fd].type == FD_TYPE_NONE) {
    regs->rax = -EBADF;
    return;
  }
  if (fd == fd2) { regs->rax = fd2; return; }
  if (t->fd_table[fd2].type == FD_TYPE_SOCKET && t->fd_table[fd2].data.sock)
    sock_close(t->fd_table[fd2].data.sock);
  t->fd_table[fd2] = t->fd_table[fd];
  regs->rax = fd2;
}

static void sys_lseek(struct registers *regs) {
  int fd = (int)regs->rdi;
  int64_t offset = (int64_t)regs->rsi;
  int whence = (int)regs->rdx;
  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type == FD_TYPE_NONE) {
    regs->rax = -EBADF;
    return;
  }
  if (t->fd_table[fd].type != FD_TYPE_FILE) { regs->rax = -ESPIPE; return; }
  vfs_node_t *node = t->fd_table[fd].data.file.node;
  int64_t base;
  switch (whence) {
    case 0: base = 0; break;
    case 1: base = (int64_t)t->fd_table[fd].data.file.offset; break;
    case 2: base = (int64_t)node->length; break;
    default: regs->rax = -EINVAL; return;
  }
  int64_t target = base + offset;
  if (target < 0) { regs->rax = -EINVAL; return; }
  t->fd_table[fd].data.file.offset = (uint64_t)target;
  regs->rax = (uint64_t)target;
}

static void fill_stat_from_node(vfs_node_t *node, struct stat *st) {
  memset(st, 0, sizeof(struct stat));
  st->st_size = node->length;
  st->st_ino = node->inode;
  st->st_uid = node->uid;
  st->st_gid = node->gid;
  uint32_t mode = (node->mode ? node->mode
                   : ((node->flags & VFS_DIRECTORY) ? DEFAULT_DIR_MODE
                                                    : DEFAULT_FILE_MODE));
  if (node->flags & VFS_DIRECTORY) mode |= S_IFDIR;
  else mode |= S_IFREG;
  st->st_mode = mode;
}

static void sys_fstat(struct registers *regs) {
  int fd = (int)regs->rdi;
  struct stat *st = (struct stat *)regs->rsi;
  thread_t *t = get_current_thread();
  if (fd < 0 || fd >= MAX_FILES || t->fd_table[fd].type != FD_TYPE_FILE) {
    regs->rax = -EBADF;
    return;
  }
  if (validate_user_pointer(st, sizeof(struct stat)) != 0) {
    regs->rax = -EFAULT;
    return;
  }
  vfs_node_t *node = t->fd_table[fd].data.file.node;
  if (!node) { regs->rax = -EBADF; return; }
  fill_stat_from_node(node, st);
  regs->rax = 0;
}

static void sys_proc_list(struct registers *regs) {
  struct procinfo *buf = (struct procinfo *)regs->rdi;
  size_t max_entries = (size_t)regs->rsi;
  if (validate_user_pointer(buf, max_entries * sizeof(struct procinfo)) != 0) {
    regs->rax = -EFAULT;
    return;
  }
  thread_t *t = get_current_thread();
  size_t count = 0;
  if (t && t->next) {
    thread_t *cur = t->next;
    // Include the caller itself first.
    if (t->state != THREAD_DEAD && max_entries > 0) {
      buf[count].pid = t->id;
      buf[count].uid = t->uid;
      buf[count].euid = t->euid;
      buf[count].state = (uint8_t)t->state;
      strncpy(buf[count].name, t->name, sizeof(buf[count].name) - 1);
      buf[count].name[sizeof(buf[count].name) - 1] = '\0';
      buf[count].cpu_ticks = t->cpu_ticks;
      buf[count].start_ticks = t->start_ticks;
      count++;
    }
    do {
      if (count >= max_entries) break;
      if (cur == t) { cur = cur->next; continue; } // caller already emitted
      if (cur->state == THREAD_DEAD) { cur = cur->next; continue; }
      buf[count].pid = cur->id;
      buf[count].uid = cur->uid;
      buf[count].euid = cur->euid;
      buf[count].state = (uint8_t)cur->state;
      strncpy(buf[count].name, cur->name, sizeof(buf[count].name) - 1);
      buf[count].name[sizeof(buf[count].name) - 1] = '\0';
      buf[count].cpu_ticks = cur->cpu_ticks;
      buf[count].start_ticks = cur->start_ticks;
      count++;
      cur = cur->next;
    } while (cur != t->next);
  }
  regs->rax = count;
}

static void sys_sysinfo(struct registers *regs) {
  struct sysinfo_k *out = (struct sysinfo_k *)regs->rdi;
  if (validate_user_pointer(out, sizeof(struct sysinfo_k)) != 0) {
    regs->rax = -EFAULT;
    return;
  }
  memset(out, 0, sizeof(struct sysinfo_k));
  out->mem_total_kb = (uint32_t)(pmm_get_total_memory() / 1024);
  out->mem_used_kb = (uint32_t)(pmm_get_used_memory() / 1024);
  out->uptime_ticks = timer_get_ticks();
  out->rx_bytes = network_devices ? network_devices->rx_bytes : 0;
  out->tx_bytes = network_devices ? network_devices->tx_bytes : 0;
  out->rx_packets = network_devices ? network_devices->rx_packets : 0;
  out->tx_packets = network_devices ? network_devices->tx_packets : 0;
  regs->rax = 0;
}

static void sys_net_ifconf(struct registers *regs) {
  struct ifinfo *buf = (struct ifinfo *)regs->rdi;
  size_t max_entries = (size_t)regs->rsi;
  if (validate_user_pointer(buf, max_entries * sizeof(struct ifinfo)) != 0) {
    regs->rax = -EFAULT;
    return;
  }
  size_t count = 0;
  for (net_dev_t *dev = network_devices; dev; dev = dev->next) {
    if (count >= max_entries) break;
    memset(&buf[count], 0, sizeof(struct ifinfo));
    // Device name: prefer deviceman name when available.
    if (dev->dev && dev->dev->name)
      strncpy(buf[count].name, dev->dev->name, sizeof(buf[count].name) - 1);
    else
      strncpy(buf[count].name, "eth", sizeof(buf[count].name) - 1);
    buf[count].name[sizeof(buf[count].name) - 1] = '\0';
    memcpy(buf[count].mac, dev->mac_addr, 6);
    // IPv4 config is global in this stack (DHCP); report per interface.
    buf[count].ip = dev->ip_addr ? dev->ip_addr : ip_get_local_ip();
    buf[count].netmask = dev->netmask ? dev->netmask : ip_get_subnet_mask();
    buf[count].gateway = dev->gateway ? dev->gateway : ip_get_default_gateway();
    buf[count].mtu = dev->mtu;
    buf[count].flags = dev->flags | IFIF_COUNTERS; // e1000 keeps real counters
    buf[count].rx_bytes = dev->rx_bytes;
    buf[count].tx_bytes = dev->tx_bytes;
    buf[count].rx_packets = dev->rx_packets;
    buf[count].tx_packets = dev->tx_packets;
    buf[count].rx_errors = dev->rx_errors;
    buf[count].tx_errors = dev->tx_errors;
    buf[count].rx_dropped = dev->rx_dropped;
    buf[count].tx_dropped = dev->tx_dropped;
    buf[count].collisions = dev->collisions;
    count++;
  }
  regs->rax = count;
}

static void sys_sock_shutdown(struct registers *regs) {
  int fd = (int)regs->rdi;
  int how = (int)regs->rsi;
  thread_t *t = get_current_thread();
  socket_t *sock;
  int rc = fd_get_socket(t, fd, &sock);
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  regs->rax = (uint64_t)sock_shutdown(sock, how);
}

static void sys_getsockname(struct registers *regs) {
  int fd = (int)regs->rdi;
  sockaddr_in_t *addr = (sockaddr_in_t *)regs->rsi;
  socklen_t *addrlen = (socklen_t *)regs->rdx;
  thread_t *t = get_current_thread();
  socket_t *sock;
  int rc = fd_get_socket(t, fd, &sock);
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  if (validate_user_pointer(addr, sizeof(sockaddr_in_t)) != 0 ||
      (addrlen && validate_user_pointer(addrlen, sizeof(socklen_t)) != 0)) {
    regs->rax = -EFAULT;
    return;
  }
  regs->rax = (uint64_t)sock_getsockname(sock, addr, addrlen);
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
  // 28-59
  syscall_table[SYS_SENDTO] = sys_sendto;
  syscall_table[SYS_RECVFROM] = sys_recvfrom;
  syscall_table[SYS_SETSOCKOPT] = sys_setsockopt;
  syscall_table[SYS_DNS_RESOLVE] = sys_dns_resolve;
  syscall_table[SYS_DEBUGMODE_GET] = sys_debugmode_get;
  syscall_table[SYS_DEBUGMODE_SET] = sys_debugmode_set;
  syscall_table[SYS_GETUID] = sys_getuid;
  syscall_table[SYS_GETEUID] = sys_geteuid;
  syscall_table[SYS_GETGID] = sys_getgid;
  syscall_table[SYS_GETEGID] = sys_getegid;
  syscall_table[SYS_UMASK] = sys_umask;
  syscall_table[SYS_CHMOD] = sys_chmod;
  syscall_table[SYS_FCHMOD] = sys_fchmod;
  syscall_table[SYS_CHOWN] = sys_chown;
  syscall_table[SYS_FCHOWN] = sys_fchown;
  syscall_table[SYS_SETUID] = sys_setuid;
  syscall_table[SYS_SETGID] = sys_setgid;
  syscall_table[SYS_SETEUID] = sys_seteuid;
  syscall_table[SYS_SETEGID] = sys_setegid;
  syscall_table[SYS_GETCWD] = sys_getcwd;
  syscall_table[SYS_CHDIR] = sys_chdir;
  syscall_table[SYS_GETPID] = sys_getpid;
  syscall_table[SYS_KILL] = sys_kill;
  syscall_table[SYS_DUP] = sys_dup;
  syscall_table[SYS_DUP2] = sys_dup2;
  syscall_table[SYS_LSEEK] = sys_lseek;
  syscall_table[SYS_FSTAT] = sys_fstat;
  syscall_table[SYS_PROC_LIST] = sys_proc_list;
  syscall_table[SYS_SYSINFO] = sys_sysinfo;
  syscall_table[SYS_NET_IFCONF] = sys_net_ifconf;
  syscall_table[SYS_SOCK_SHUTDOWN] = sys_sock_shutdown;
  syscall_table[SYS_GETSOCKNAME] = sys_getsockname;
  klog(LOG_INFO, "Syscall handler expanded.");
}

// Registry consistency audit: every registry entry marked impl=yes/partial
// with a number < 256 must have a dispatch entry (except send which reuses
// the write path). Called late in boot (see kernel.c).
void syscall_audit_table() {
  int missing = 0;
  for (int i = 0; i < 256; i++) {
    if (syscall_table[i] == 0 && i <= SYS_GETSOCKNAME &&
        i != SYS_SEND) { // send dispatches to sys_write path via SYS_SEND slot
      // Only complain about numbers the registry declares implemented.
      // (Reserved 60+ are intentionally absent.)
      if (i >= SYS_SENDTO && i <= SYS_GETSOCKNAME) missing++;
    }
  }
  if (missing)
    klog(LOG_WARN, "syscall_audit: %d registry syscalls lack dispatch entries", missing);
  else
    klog(LOG_INFO, "syscall_audit: dispatch table covers registry range");
}

void syscall_handler(struct registers *regs) {
  uint64_t syscall_num = regs->rax;
  if (syscall_num < 256 && syscall_table[syscall_num] != 0) {
    syscall_table[syscall_num](regs);
  } else {
    klog(LOG_WARN, "Unknown syscall received: %llu", syscall_num);
    regs->rax = (uint64_t)-ENOSYS; // documented error contract
  }
}
