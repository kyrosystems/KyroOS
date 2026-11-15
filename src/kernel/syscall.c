#define KYRO_KERNEL_DISPLAY
#include "display.h"
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
#include "pipe.h"
#include "rtc.h"
#include "procinfo.h"
#include "kyrofs.h"
#include "ide.h"

// Forward declarations from vmm.h
extern pml4_t *vmm_create_address_space(void);
extern void vmm_destroy_address_space(pml4_t *pml4);

// HHDM offset from kernel.c, needed for V_TO_P / P_TO_V
extern uint64_t kernel_hhdm_offset;
#define V_TO_P(v) ((void *)((uint64_t)(v) - kernel_hhdm_offset))
#define P_TO_V(p) ((void *)((uint64_t)(p) + kernel_hhdm_offset))

typedef void (*syscall_func_t)(struct registers *regs);
static syscall_func_t syscall_table[256];

static void sys_exit(struct registers *regs) {
  thread_t *t = get_current_thread();
  if (t) {
    t->exit_status = (int)regs->rdi;
  }
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
    regs->rax = (uint64_t)-EBADF; // Invalid FD or not open
    return;
  }

  if (validate_user_pointer(buffer, size) != 0) {
      klog(LOG_ERROR, "SYSCALL_EXIT: sys_write: Invalid buffer pointer %p", buffer);
      regs->rax = (uint64_t)-EFAULT;
      return;
  }

  if (t->fd_table[fd].type == FD_TYPE_FILE) {
    if (!(t->fd_table[fd].data.file.flags & O_WRONLY)) { regs->rax = -EBADF; return; }
    if (t->fd_table[fd].data.file.node->flags & VFS_DIRECTORY) { regs->rax = -EISDIR; return; }
    uint64_t current_offset = FD_OFFSET(t->fd_table[fd]);
    if (t->fd_table[fd].data.file.flags & O_APPEND) {
      current_offset =
          t->fd_table[fd].data.file.node->length; // For append, write at end
    }
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_write: Writing to file FD %d, offset %llu, size %u", fd, current_offset, size);
    regs->rax = vfs_write(t->fd_table[fd].data.file.node, current_offset, size,
                          (uint8_t *)buffer);
    if ((int64_t)regs->rax >= 0) {
      FD_OFFSET(t->fd_table[fd]) =
          current_offset + regs->rax; // Update offset
      klog(LOG_DEBUG, "SYSCALL_EXIT: sys_write: Wrote %llu bytes to FD %d. New offset %llu.", regs->rax, fd, FD_OFFSET(t->fd_table[fd]));
    } else {
      klog(LOG_ERROR, "SYSCALL_EXIT: sys_write: vfs_write failed for FD %d. Returning -1.", fd);
    }
  } else if (t->fd_table[fd].type == FD_TYPE_SOCKET) {
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_write: Writing to socket FD %d, size %u", fd, size);
    regs->rax = sock_send(t->fd_table[fd].data.sock, buffer, size,
                          0);
    klog(LOG_DEBUG, "SYSCALL_EXIT: sys_write: Sent %llu bytes to socket FD %d.", regs->rax, fd);
  } else if (t->fd_table[fd].type == FD_TYPE_PIPE) {
    if (!t->fd_table[fd].data.pipe.is_write) {
      regs->rax = (uint64_t)-EBADF;
      return;
    }
    regs->rax = (uint64_t)pipe_write(t->fd_table[fd].data.pipe.pipe, buffer, size);
  } else {
    klog(LOG_ERROR, "SYSCALL_EXIT: sys_write: Invalid FD type %d for FD %d. Returning -1.", t->fd_table[fd].type, fd);
    regs->rax = (uint64_t)-1; // Invalid FD type
  }
}

static void canonicalize_path(const char *cwd, const char *input, char *output, size_t max_len) {
  char combined[MAX_FILENAME_LEN * 2];
  if (!input || input[0] == '\0') {
    input = ".";
  }
  if (input[0] == '/') {
    strncpy(combined, input, sizeof(combined) - 1);
  } else {
    const char *c = (cwd && cwd[0]) ? cwd : "/";
    size_t clen = strlen(c);
    ksprintf(combined, "%s%s%s", c, (clen > 0 && c[clen - 1] == '/') ? "" : "/", input);
  }
  combined[sizeof(combined) - 1] = '\0';

  char stack[32][64];
  int top = 0;
  char *p = combined;

  while (*p) {
    while (*p == '/') p++;
    if (!*p) break;
    char *start = p;
    while (*p && *p != '/') p++;
    size_t len = p - start;
    if (len >= 64) len = 63;
    char token[64];
    memcpy(token, start, len);
    token[len] = '\0';

    if (strcmp(token, ".") == 0) {
      continue;
    } else if (strcmp(token, "..") == 0) {
      if (top > 0) top--;
      continue;
    } else {
      if (top < 32) {
        strncpy(stack[top++], token, 63);
        stack[top - 1][63] = '\0';
      }
    }
  }

  if (top == 0) {
    strncpy(output, "/", max_len - 1);
  } else {
    output[0] = '\0';
    for (int i = 0; i < top; i++) {
      size_t cur_len = strlen(output);
      if (cur_len + 1 + strlen(stack[i]) < max_len) {
        output[cur_len] = '/';
        strcpy(output + cur_len + 1, stack[i]);
      }
    }
  }
  output[max_len - 1] = '\0';
}

static vfs_node_t *resolve_for_thread(const char *path) {
  thread_t *t = get_current_thread();
  const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
  static char canon[MAX_FILENAME_LEN];
  canonicalize_path(cwd, path, canon, sizeof(canon));
  return vfs_resolve_path(vfs_root, canon);
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
      FD_OFFSET(t->fd_table[i]) =
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
    regs->rax = (uint64_t)-EBADF;
    return;
  }

  fd_release(&t->fd_table[fd]);
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
    regs->rax = (uint64_t)-EBADF;
    return;
  }

  if (validate_user_write(buf, size) != 0) {
      klog(LOG_ERROR, "SYSCALL_EXIT: sys_read: Invalid buffer pointer %p", buf);
      regs->rax = (uint64_t)-EFAULT;
      return;
  }

  if (t->fd_table[fd].type == FD_TYPE_FILE) {
    if (!(t->fd_table[fd].data.file.flags & O_RDONLY)) { regs->rax = -EBADF; return; }
    if (t->fd_table[fd].data.file.node->flags & VFS_DIRECTORY) { regs->rax = -EISDIR; return; }
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_read: Reading from file FD %d, offset %llu, size %u", fd, FD_OFFSET(t->fd_table[fd]), size);
    regs->rax =
        vfs_read(t->fd_table[fd].data.file.node,
                 FD_OFFSET(t->fd_table[fd]), size, (uint8_t *)buf);
    if ((int64_t)regs->rax >= 0) {
      FD_OFFSET(t->fd_table[fd]) += regs->rax; // Update offset
      klog(LOG_DEBUG, "SYSCALL_EXIT: sys_read: Read %llu bytes from FD %d. New offset %llu.", regs->rax, fd, FD_OFFSET(t->fd_table[fd]));
    } else {
      klog(LOG_ERROR, "SYSCALL_EXIT: sys_read: vfs_read failed for FD %d. Returning -1.", fd);
    }
  } else if (t->fd_table[fd].type == FD_TYPE_SOCKET) {
    klog(LOG_DEBUG, "SYSCALL_DEBUG: sys_read: Reading from socket FD %d, size %u", fd, size);
    regs->rax =
        sock_recv(t->fd_table[fd].data.sock, buf, size, 0);
    klog(LOG_DEBUG, "SYSCALL_EXIT: sys_read: Received %llu bytes from socket FD %d.", regs->rax, fd);
  } else if (t->fd_table[fd].type == FD_TYPE_PIPE) {
    if (t->fd_table[fd].data.pipe.is_write) {
      regs->rax = (uint64_t)-EBADF;
      return;
    }
    regs->rax = (uint64_t)pipe_read(t->fd_table[fd].data.pipe.pipe, buf, size);
  } else {
    klog(LOG_ERROR, "SYSCALL_EXIT: sys_read: Invalid FD type %d for FD %d. Returning -1.", t->fd_table[fd].type, fd);
    regs->rax = (uint64_t)-1; // Invalid FD type
  }
}

static void fill_stat_from_node(vfs_node_t *node, struct stat *st);

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

  if (validate_user_write(stat_buf, sizeof(struct stat)) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }

  vfs_node_t *node = resolve_for_thread(path);
  if (!node) {
    regs->rax = -ENOENT;
    return;
  }

  if (node->stat) {
    regs->rax = node->stat(node, stat_buf);
  } else {
    fill_stat_from_node(node, stat_buf);
    regs->rax = 0;
  }
}

static void sys_mkdir(struct registers *regs) {
  char *user_path = (char *)regs->rdi;
  char path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_path, 1) != 0) {
    regs->rax = (uint64_t)-EFAULT;
    return;
  }
  strncpy(path, user_path, MAX_FILENAME_LEN - 1);
  path[MAX_FILENAME_LEN - 1] = '\0';

  thread_t *t = get_current_thread();
  const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
  char full_path[MAX_FILENAME_LEN];
  canonicalize_path(cwd, path, full_path, sizeof(full_path));

  char parent_path[MAX_FILENAME_LEN];
  char child_name[MAX_FILENAME_LEN];
  char *last_slash = strrchr(full_path, '/');
  if (!last_slash || last_slash == full_path) {
    strncpy(parent_path, "/", sizeof(parent_path) - 1);
    strncpy(child_name, last_slash ? last_slash + 1 : full_path, sizeof(child_name) - 1);
  } else {
    size_t plen = last_slash - full_path;
    strncpy(parent_path, full_path, plen);
    parent_path[plen] = '\0';
    strncpy(child_name, last_slash + 1, sizeof(child_name) - 1);
  }
  parent_path[sizeof(parent_path) - 1] = '\0';
  child_name[sizeof(child_name) - 1] = '\0';

  if (child_name[0] == '\0' || strcmp(child_name, ".") == 0 || strcmp(child_name, "..") == 0) {
    regs->rax = -EINVAL;
    return;
  }

  vfs_node_t *parent_node = vfs_resolve_path(vfs_root, parent_path);
  if (!parent_node) {
    regs->rax = -ENOENT;
    return;
  }
  if (!(parent_node->flags & VFS_DIRECTORY)) {
    regs->rax = -ENOTDIR;
    return;
  }
  if (vfs_finddir(parent_node, child_name)) {
    regs->rax = -EEXIST;
    return;
  }
  int ac = vfs_check_parent_write(parent_node);
  if (ac != 0) {
    regs->rax = (uint64_t)ac;
    return;
  }

  uint16_t mode = t ? (uint16_t)(~t->umask & 0777) : 0755;
  regs->rax = vfs_mkdir(parent_node, child_name, mode);
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

  if (validate_user_write(dir_entry, sizeof(struct dirent)) != 0) {
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
    regs->rax = (uint64_t)-EFAULT;
    return;
  }
  strncpy(path, user_path, MAX_FILENAME_LEN - 1);
  path[MAX_FILENAME_LEN - 1] = '\0';

  thread_t *t = get_current_thread();
  const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
  char full_path[MAX_FILENAME_LEN];
  canonicalize_path(cwd, path, full_path, sizeof(full_path));

  char parent_path[MAX_FILENAME_LEN];
  char child_name[MAX_FILENAME_LEN];
  char *last_slash = strrchr(full_path, '/');
  if (!last_slash || last_slash == full_path) {
    strncpy(parent_path, "/", sizeof(parent_path) - 1);
    strncpy(child_name, last_slash ? last_slash + 1 : full_path, sizeof(child_name) - 1);
  } else {
    size_t plen = last_slash - full_path;
    strncpy(parent_path, full_path, plen);
    parent_path[plen] = '\0';
    strncpy(child_name, last_slash + 1, sizeof(child_name) - 1);
  }
  parent_path[sizeof(parent_path) - 1] = '\0';
  child_name[sizeof(child_name) - 1] = '\0';

  vfs_node_t *parent_node = vfs_resolve_path(vfs_root, parent_path);
  if (!parent_node) {
    regs->rax = -ENOENT;
    return;
  }
  int ac = vfs_check_parent_write(parent_node);
  if (ac != 0) {
    regs->rax = (uint64_t)ac;
    return;
  }

  regs->rax = vfs_remove(parent_node, child_name);
}

static void sys_rmdir(struct registers *regs) {
  char *user_path = (char *)regs->rdi;
  char path[MAX_FILENAME_LEN];
  if (validate_user_pointer(user_path, 1) != 0) {
    regs->rax = (uint64_t)-EFAULT;
    return;
  }
  strncpy(path, user_path, MAX_FILENAME_LEN - 1);
  path[MAX_FILENAME_LEN - 1] = '\0';

  thread_t *t = get_current_thread();
  const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
  char full_path[MAX_FILENAME_LEN];
  canonicalize_path(cwd, path, full_path, sizeof(full_path));

  char parent_path[MAX_FILENAME_LEN];
  char child_name[MAX_FILENAME_LEN];
  char *last_slash = strrchr(full_path, '/');
  if (!last_slash || last_slash == full_path) {
    strncpy(parent_path, "/", sizeof(parent_path) - 1);
    strncpy(child_name, last_slash ? last_slash + 1 : full_path, sizeof(child_name) - 1);
  } else {
    size_t plen = last_slash - full_path;
    strncpy(parent_path, full_path, plen);
    parent_path[plen] = '\0';
    strncpy(child_name, last_slash + 1, sizeof(child_name) - 1);
  }
  parent_path[sizeof(parent_path) - 1] = '\0';
  child_name[sizeof(child_name) - 1] = '\0';

  vfs_node_t *parent_node = vfs_resolve_path(vfs_root, parent_path);
  if (!parent_node) {
    regs->rax = -ENOENT;
    return;
  }
  int ac = vfs_check_parent_write(parent_node);
  if (ac != 0) {
    regs->rax = (uint64_t)ac;
    return;
  }

  regs->rax = vfs_rmdir(parent_node, child_name, 0);
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

  if (addr && validate_user_write(addr, sizeof(sockaddr_in_t)) != 0) {
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

  if (validate_user_write(buf, len) != 0) {
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

  if (validate_user_pointer(data, len) != 0 || validate_user_write(hash_out, 32) != 0) {
      regs->rax = (uint64_t)-1;
      return;
  }

  sha256_hash(data, len, hash_out);
  regs->rax = 0; // Success
}

static void sys_sbrk(struct registers *regs);

static void sys_malloc(struct registers *regs) {
  // Use sys_sbrk to ensure memory is mapped into user-space with PAGE_USER | PAGE_WRITE
  size_t size = (size_t)regs->rdi;
  if (size == 0) {
    regs->rax = 0;
    return;
  }
  regs->rdi = (uint64_t)size;
  sys_sbrk(regs);
}

static void sys_free(struct registers *regs) {
  (void)regs;
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

static void sys_display_ctl(struct registers *regs) {
    regs->rax = display_control(regs->rdi, (void *)regs->rsi, regs->rdx);
}

static void sys_gfx_get_fb_info(struct registers *regs) {
  if (display_active() && !display_is_server(get_current_thread())) { regs->rax = -EBUSY; return; }
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

  if (validate_user_write(user_info_ptr, sizeof(user_fb_info_t)) != 0) {
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
      vmm_map_page(get_current_thread()->pml4, virt, phys,
                   PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
    klog(LOG_INFO, "SYSCALL_GFX: Mapped framebuffer (%llu bytes) to %p",
         fb_size, (void *)USER_FRAMEBUFFER_VADDR);

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

    fb_set_graphics_owner(get_current_thread()->id);
    regs->rax = 0; // Success
  } else {
    klog(LOG_ERROR,
         "SYSCALL_GFX: kernel_fb is NULL or kernel_fb->address is NULL.");
    regs->rax = -1; // Failure
  }
}

static void sys_input_poll_event(struct registers *regs) {
  event_t *user_event = (event_t *)regs->rdi;
  if (validate_user_write(user_event, sizeof(event_t)) != 0) {
      regs->rax = 0;
      return;
  }

  int surface_event = display_poll(get_current_thread(), user_event);
  if (surface_event >= 0) { regs->rax = surface_event; return; }

  /* Keyboard focus: while a userspace program runs, the shell thread must
     not race it for console events (that made e.g. sysmon unable to see 'q'). */
  {
    thread_t *self = get_current_thread();
    if (console_focus && console_focus != self) { regs->rax = 0; return; }
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
      uint64_t start = (old_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
      for (uint64_t addr = start; addr < new_brk; addr += PAGE_SIZE) {
          if (!vmm_get_phys_addr(t->pml4, (void*)addr)) {
              void *phys = pmm_alloc_page();
              if (!phys) {
                  regs->rax = (uint64_t)-1;
                  return;
              }
              memset((void*)P_TO_V(phys), 0, PAGE_SIZE);
              vmm_map_page(t->pml4, (void*)addr, phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
          }
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
  if (validate_user_write(buf, len) != 0) { regs->rax = -EFAULT; return; }
  if (addr && validate_user_write(addr, sizeof(sockaddr_in_t)) != 0) {
    regs->rax = -EFAULT;
    return;
  }
  if (addrlen && validate_user_write(addrlen, sizeof(socklen_t)) != 0) {
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
  if (t->euid != 0 && t->uid != 0 && t->id > 2) {
    if (uid != t->uid) { regs->rax = -EPERM; return; }
  }
  t->uid = t->euid = uid;
  regs->rax = 0;
}

static void sys_setgid(struct registers *regs) {
  uint32_t gid = (uint32_t)regs->rdi;
  thread_t *t = get_current_thread();
  if (t->euid != 0 && t->uid != 0 && t->id > 2) {
    if (gid != t->gid) { regs->rax = -EPERM; return; }
  }
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
  if (validate_user_write(buf, size) != 0) { regs->rax = -EFAULT; return; }
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
  thread_t *t = get_current_thread();
  const char *cwd = (t && t->cwd[0]) ? t->cwd : "/";
  char canon[MAX_FILENAME_LEN];
  canonicalize_path(cwd, path, canon, sizeof(canon));
  vfs_node_t *node = vfs_resolve_path(vfs_root, canon);
  if (!node) { regs->rax = -ENOENT; return; }
  if (!(node->flags & VFS_DIRECTORY)) { regs->rax = -ENOTDIR; return; }
  rc = vfs_check_access(node, 1); // need search permission on the directory
  if (rc != 0) { regs->rax = (uint64_t)rc; return; }
  strncpy(t->cwd, canon, sizeof(t->cwd) - 1);
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
  if (sig < 1 || sig >= 32) { regs->rax = -EINVAL; return; }
  if (pid == t->id && (sig == SIGKILL || sig == SIGTERM)) { thread_exit(); return; }

  extern thread_t *scheduler_find_thread(uint64_t pid);
  thread_t *target = scheduler_find_thread(pid);
  if (!target) { regs->rax = -ESRCH; return; }
  if (t->euid != 0 && t->euid != target->euid) { regs->rax = -EPERM; return; }

  if (sig == SIGKILL) {
    target->state = THREAD_DEAD;
  } else if (target->sig_handlers[sig] == SIG_IGN) {
    // Ignored
  } else if (target->sig_handlers[sig] != SIG_DFL) {
    target->sig_pending |= (1 << sig);
  } else {
    if (sig != SIGCHLD) {
      target->exit_status = 128 + sig;
      target->state = THREAD_DEAD;
    }
  }
  regs->rax = 0;
}

static void sys_sigaction(struct registers *regs) {
  int signum = (int)regs->rdi;
  const struct sigaction *act = (const struct sigaction *)regs->rsi;
  struct sigaction *oldact = (struct sigaction *)regs->rdx;
  thread_t *t = get_current_thread();

  if (signum < 1 || signum >= 32 || signum == SIGKILL || !t) {
    regs->rax = (uint64_t)-EINVAL;
    return;
  }
  if (oldact) {
    if (validate_user_write(oldact, sizeof(struct sigaction)) != 0) {
      regs->rax = (uint64_t)-EFAULT;
      return;
    }
    oldact->sa_handler = t->sig_handlers[signum];
    oldact->sa_mask = t->sig_mask;
    oldact->sa_flags = 0;
  }
  if (act) {
    if (validate_user_pointer(act, sizeof(struct sigaction)) != 0) {
      regs->rax = (uint64_t)-EFAULT;
      return;
    }
    t->sig_handlers[signum] = act->sa_handler;
  }
  regs->rax = 0;
}

static void sys_sigprocmask(struct registers *regs) {
  int how = (int)regs->rdi;
  const uint32_t *set = (const uint32_t *)regs->rsi;
  uint32_t *oldset = (uint32_t *)regs->rdx;
  thread_t *t = get_current_thread();
  if (!t) { regs->rax = (uint64_t)-ESRCH; return; }

  if (oldset) {
    if (validate_user_write(oldset, sizeof(uint32_t)) != 0) {
      regs->rax = (uint64_t)-EFAULT;
      return;
    }
    *oldset = t->sig_mask;
  }
  if (set) {
    if (validate_user_pointer(set, sizeof(uint32_t)) != 0) {
      regs->rax = (uint64_t)-EFAULT;
      return;
    }
    if (how == SIG_BLOCK) t->sig_mask |= *set;
    else if (how == SIG_UNBLOCK) t->sig_mask &= ~(*set);
    else if (how == SIG_SETMASK) t->sig_mask = *set;
    else { regs->rax = (uint64_t)-EINVAL; return; }
  }
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
      if (fd_duplicate(&t->fd_table[i], &t->fd_table[fd]) < 0) {
        regs->rax = -ENOMEM; return;
      }
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
  if (fd_duplicate(&t->fd_table[fd2], &t->fd_table[fd]) < 0) {
    regs->rax = -ENOMEM; return;
  }
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
    case 1: base = (int64_t)FD_OFFSET(t->fd_table[fd]); break;
    case 2: base = (int64_t)node->length; break;
    default: regs->rax = -EINVAL; return;
  }
  if (base < 0 || (offset < 0 && offset < -base) ||
      (offset > 0 && offset > INT64_MAX - base)) { regs->rax = -EINVAL; return; }
  int64_t target = base + offset;
  if (target < 0) { regs->rax = -EINVAL; return; }
  FD_OFFSET(t->fd_table[fd]) = (uint64_t)target;
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
  if (validate_user_write(st, sizeof(struct stat)) != 0) {
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
  if (validate_user_write(buf, max_entries * sizeof(struct procinfo)) != 0) {
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
  if (validate_user_write(out, sizeof(struct sysinfo_k)) != 0) {
    regs->rax = -EFAULT;
    return;
  }
  memset(out, 0, sizeof(struct sysinfo_k));
  out->mem_total_kb = (uint32_t)(pmm_get_total_memory() / 1024);
  out->mem_used_kb = (uint32_t)(pmm_get_used_memory() / 1024);
  out->uptime_ticks = timer_get_ticks();

  uint32_t procs = 0;
  thread_t *t = get_current_thread();
  if (t) {
    thread_t *cur = t;
    do {
      if (cur->state != THREAD_DEAD) procs++;
      cur = cur->next;
    } while (cur && cur != t);
  }
  out->procs_total = procs ? procs : 1;

  out->rx_bytes = network_devices ? network_devices->rx_bytes : 0;
  out->tx_bytes = network_devices ? network_devices->tx_bytes : 0;
  out->rx_packets = network_devices ? network_devices->rx_packets : 0;
  out->tx_packets = network_devices ? network_devices->tx_packets : 0;

  out->cpu_idle_ticks = g_cpu_idle_ticks;
  out->cpu_busy_ticks = g_cpu_busy_ticks;
  out->fs_used_bytes = kyrofs_get_total_bytes_used();
  out->fs_total_bytes = ide_get_primary_capacity();

  rtc_time_t rt = rtc_get_time();
  out->rtc_sec = rt.second;
  out->rtc_min = rt.minute;
  out->rtc_hour = rt.hour;
  out->rtc_day = rt.day;
  out->rtc_mon = rt.month;
  out->rtc_year = rt.year;

  regs->rax = 0;
}

static void sys_net_ifconf(struct registers *regs) {
  struct ifinfo *buf = (struct ifinfo *)regs->rdi;
  size_t max_entries = (size_t)regs->rsi;
  if (validate_user_write(buf, max_entries * sizeof(struct ifinfo)) != 0) {
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
  if (validate_user_write(addr, sizeof(sockaddr_in_t)) != 0 ||
      (addrlen && validate_user_write(addrlen, sizeof(socklen_t)) != 0)) {
    regs->rax = -EFAULT;
    return;
  }
  regs->rax = (uint64_t)sock_getsockname(sock, addr, addrlen);
}

static void sys_fork(struct registers *regs) {
  extern thread_t *thread_fork(struct registers *parent_regs);
  thread_t *child = thread_fork(regs);
  if (!child) {
    regs->rax = (uint64_t)-ENOMEM;
    return;
  }
  regs->rax = child->id;
}

static void sys_waitpid(struct registers *regs) {
  int64_t pid = (int32_t)regs->rdi;
  int *status = (int *)regs->rsi;
  int options = (int)regs->rdx;
  if (status && validate_user_write(status, sizeof(int)) != 0) {
    regs->rax = (uint64_t)-EFAULT;
    return;
  }
  if ((options & ~1) || pid < -1) { regs->rax = -EINVAL; return; }
  int st = 0;
  int ret = scheduler_waitpid((uint64_t)pid, status ? &st : NULL, options);
  if (ret >= 0 && status) {
    *status = st;
  }
  regs->rax = (uint64_t)ret;
}

static void sys_execve(struct registers *regs) {
  const char *user_path = (const char *)regs->rdi;
  char **user_argv = (char **)regs->rsi;
  char path[MAX_FILENAME_LEN];
  int rc = copy_user_string(path, user_path, sizeof(path));
  if (rc) { regs->rax = (uint64_t)rc; return; }
  int argc = 0;
  char *kargv[32];
  char kargs[32][128];
  if (user_argv) {
    for (; argc < 32; argc++) {
      char *arg;
      if (copy_from_user(&arg, &user_argv[argc], sizeof(arg))) {
        regs->rax = -EFAULT; return;
      }
      if (!arg) break;
      rc = copy_user_string(kargs[argc], arg, sizeof(kargs[argc]));
      if (rc) { regs->rax = rc == -ENAMETOOLONG ? -E2BIG : (uint64_t)rc; return; }
      kargv[argc] = kargs[argc];
    }
    if (argc == 32) { regs->rax = -E2BIG; return; }
  }
  if (!argc) { kargv[0] = path; argc = 1; }
  kargv[argc] = NULL;

  vfs_node_t *node = resolve_for_thread(path);
  if (!node) {
    char bin_path[MAX_FILENAME_LEN];
    ksprintf(bin_path, "/bin/%s", path);
    node = vfs_resolve_path(vfs_root, bin_path);
    if (node) {
      strncpy(path, bin_path, sizeof(path) - 1);
    }
  }
  if (!node) {
    regs->rax = (uint64_t)-ENOENT;
    return;
  }

  struct thread *th = elf_exec_as_thread_handle(path, argc, kargv);
  if (!th) {
    regs->rax = (uint64_t)-ENOEXEC;
    return;
  }
  thread_t *cur = get_current_thread();
  if (cur) {
    th->parent_id = cur->id;
    for (int i = 0; i < MAX_FILES; i++) {
      if (fd_duplicate(&th->fd_table[i], &cur->fd_table[i]) < 0) {
        th->state = THREAD_DEAD;
        regs->rax = -ENOMEM;
        return;
      }
    }
  }
  regs->rax = th->id;
}

static void sys_pipe(struct registers *regs) {
  int *user_fds = (int *)regs->rdi;
  thread_t *t = get_current_thread();
  if (!t || validate_user_write(user_fds, sizeof(int) * 2) != 0) {
    regs->rax = (uint64_t)-EFAULT;
    return;
  }
  int fd0 = -1, fd1 = -1;
  for (int i = 0; i < MAX_FILES; i++) {
    if (t->fd_table[i].type == FD_TYPE_NONE) {
      if (fd0 == -1) fd0 = i;
      else if (fd1 == -1) { fd1 = i; break; }
    }
  }
  if (fd0 == -1 || fd1 == -1) {
    regs->rax = (uint64_t)-EMFILE;
    return;
  }
  pipe_t *p = pipe_create();
  if (!p) {
    regs->rax = (uint64_t)-ENOMEM;
    return;
  }
  t->fd_table[fd0].type = FD_TYPE_PIPE;
  t->fd_table[fd0].data.pipe.pipe = p;
  t->fd_table[fd0].data.pipe.is_write = 0;

  t->fd_table[fd1].type = FD_TYPE_PIPE;
  t->fd_table[fd1].data.pipe.pipe = p;
  t->fd_table[fd1].data.pipe.is_write = 1;

  user_fds[0] = fd0;
  user_fds[1] = fd1;
  regs->rax = 0;
}

#define MMAP_BASE 0x600000000000ULL
#define MMAP_END  0x700000000000ULL

static bool mmap_range(uint64_t addr, size_t len) {
  return len && !(addr & (PAGE_SIZE - 1)) && addr >= MMAP_BASE &&
         addr < MMAP_END && len <= MMAP_END - addr;
}

static uint64_t mmap_protection(int prot) {
  uint64_t flags = PAGE_PRESENT | PAGE_OWNED;
  if (prot) flags |= PAGE_USER; // PROT_NONE retains ownership without user access
  if (prot & 2) flags |= PAGE_WRITE;
  if (!(prot & 4) && vmm_nx_supported) flags |= PAGE_NO_EXEC;
  return flags;
}

static void sys_mmap(struct registers *regs) {
  uint64_t addr = regs->rdi;
  size_t len = regs->rsi;
  int prot = regs->rdx, flags = regs->r10;
  thread_t *t = get_current_thread();
  if (!len || len > MMAP_END - MMAP_BASE || (prot & ~7)) {
    regs->rax = -EINVAL; return;
  }
  // File-backed and shared mappings need page ownership/refcount support
  if (flags != (0x02 | 0x20)) { regs->rax = -EOPNOTSUPP; return; }
  if (regs->r9) { regs->rax = -EINVAL; return; }
  if (!t || !t->pml4) { regs->rax = -ENOMEM; return; }
  len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  uint64_t vaddr = mmap_range(addr, len) ? addr : MMAP_BASE;
  // An address is a hint, never permission to overwrite an existing mapping
  for (;;) {
    if (!mmap_range(vaddr, len)) { regs->rax = -ENOMEM; return; }
    size_t i;
    for (i = 0; i < len; i += PAGE_SIZE) {
      if (vmm_get_phys_addr(t->pml4, (void *)(vaddr + i))) break;
    }
    if (i == len) break;
    vaddr += i + PAGE_SIZE;
  }
  size_t mapped = 0;
  for (; mapped < len; mapped += PAGE_SIZE) {
    void *phys = pmm_alloc_page();
    if (!phys) break;
    memset(vmm_phys_to_virt(phys), 0, PAGE_SIZE);
    vmm_map_page(t->pml4, (void *)(vaddr + mapped), phys, mmap_protection(prot));
  }
  if (mapped != len) {
    for (size_t i = 0; i < mapped; i += PAGE_SIZE)
      pmm_free_page(vmm_unmap_page(t->pml4, (void *)(vaddr + i)));
    regs->rax = -ENOMEM; return;
  }
  regs->rax = vaddr;
}

static void sys_munmap(struct registers *regs) {
  uint64_t addr = regs->rdi;
  size_t len = regs->rsi;
  thread_t *t = get_current_thread();
  if (!t || !t->pml4 || !mmap_range(addr, len)) { regs->rax = -EINVAL; return; }
  for (size_t i = 0; i < len; i += PAGE_SIZE) {
    void *phys = vmm_unmap_page(t->pml4, (void *)(addr + i));
    if (phys) pmm_free_page(phys);
  }
  regs->rax = 0;
}

static void sys_mprotect(struct registers *regs) {
  uint64_t addr = regs->rdi;
  size_t len = regs->rsi;
  int prot = regs->rdx;
  thread_t *t = get_current_thread();
  if (!t || !t->pml4 || !mmap_range(addr, len) || (prot & ~7)) {
    regs->rax = -EINVAL; return;
  }
  // Validate the complete range before changing any page
  for (size_t i = 0; i < len; i += PAGE_SIZE) {
    if (!vmm_get_phys_addr(t->pml4, (void *)(addr + i))) {
      regs->rax = -ENOMEM; return;
    }
  }
  for (size_t i = 0; i < len; i += PAGE_SIZE) {
    void *va = (void *)(addr + i);
    uint64_t phys = vmm_get_phys_addr(t->pml4, va);
    vmm_map_page(t->pml4, va, (void *)phys, mmap_protection(prot));
  }
  regs->rax = 0;
}

static char kernel_hostname[64] = "kyroos";

static void sys_gethostname(struct registers *regs) {
  char *user_name = (char *)regs->rdi;
  size_t len = (size_t)regs->rsi;
  if (validate_user_write(user_name, len) != 0) {
    regs->rax = (uint64_t)-EFAULT;
    return;
  }
  size_t hlen = strlen(kernel_hostname);
  if (len < hlen + 1) {
    regs->rax = (uint64_t)-ENAMETOOLONG;
    return;
  }
  strncpy(user_name, kernel_hostname, len);
  regs->rax = 0;
}

static void sys_sethostname(struct registers *regs) {
  const char *user_name = (const char *)regs->rdi;
  size_t len = (size_t)regs->rsi;
  thread_t *t = get_current_thread();
  if (!t || t->euid != 0) {
    regs->rax = (uint64_t)-EPERM;
    return;
  }
  if (len >= sizeof(kernel_hostname)) {
    regs->rax = (uint64_t)-EINVAL;
    return;
  }
  if (validate_user_pointer((void*)user_name, len) != 0) {
    regs->rax = (uint64_t)-EFAULT;
    return;
  }
  strncpy(kernel_hostname, user_name, len);
  kernel_hostname[len] = '\0';
  regs->rax = 0;
}

#include "pci.h"

static void sys_get_gpu_info(struct registers *regs) {
  gpu_info_t *user_gpu = (gpu_info_t *)regs->rdi;
  if (validate_user_write(user_gpu, sizeof(gpu_info_t)) != 0) {
    regs->rax = (uint64_t)-EFAULT;
    return;
  }

  gpu_info_t info;
  memset(&info, 0, sizeof(info));

  extern pci_device_node_t *pci_devices;
  pci_device_node_t *cur = pci_devices;
  pci_device_t *gpu_dev = NULL;

  while (cur) {
    if (cur->device.class_code == 0x03) {
      gpu_dev = &cur->device;
      break;
    }
    cur = cur->next;
  }

  if (gpu_dev) {
    info.vendor_id = gpu_dev->vendor_id;
    info.device_id = gpu_dev->device_id;
    info.bus = gpu_dev->bus;
    info.slot = gpu_dev->device;
    info.func = gpu_dev->func;
    info.bar0_base = gpu_dev->bars[0].base;
    info.bar1_base = gpu_dev->bars[1].base;

    uint64_t max_vram = 0;
    for (int i = 0; i < 6; i++) {
      if (gpu_dev->bars[i].type == 0 && gpu_dev->bars[i].size > max_vram) {
        max_vram = gpu_dev->bars[i].size;
      }
    }
    info.vram_size_bytes = max_vram;

    if (gpu_dev->vendor_id == 0x15AD && (gpu_dev->device_id == 0x0405 || gpu_dev->device_id == 0x0406)) {
      strncpy(info.vendor_name, "VMware, Inc.", sizeof(info.vendor_name) - 1);
      strncpy(info.model_name, "SVGA II Adapter (VMware GPU)", sizeof(info.model_name) - 1);
    } else if (gpu_dev->vendor_id == 0x1234 && gpu_dev->device_id == 0x1111) {
      strncpy(info.vendor_name, "QEMU / Bochs", sizeof(info.vendor_name) - 1);
      strncpy(info.model_name, "Standard VGA Graphics Adapter", sizeof(info.model_name) - 1);
    } else if (gpu_dev->vendor_id == 0x1AF4 && (gpu_dev->device_id == 0x1050 || gpu_dev->device_id == 0x1041)) {
      strncpy(info.vendor_name, "Red Hat, Inc.", sizeof(info.vendor_name) - 1);
      strncpy(info.model_name, "VirtIO GPU Accelerator", sizeof(info.model_name) - 1);
    } else if (gpu_dev->vendor_id == 0x8086) {
      strncpy(info.vendor_name, "Intel Corporation", sizeof(info.vendor_name) - 1);
      strncpy(info.model_name, "HD/UHD/Iris Graphics Accelerator", sizeof(info.model_name) - 1);
    } else if (gpu_dev->vendor_id == 0x10DE) {
      strncpy(info.vendor_name, "NVIDIA Corporation", sizeof(info.vendor_name) - 1);
      strncpy(info.model_name, "GeForce / RTX Graphics GPU", sizeof(info.model_name) - 1);
    } else if (gpu_dev->vendor_id == 0x1002) {
      strncpy(info.vendor_name, "AMD / ATI", sizeof(info.vendor_name) - 1);
      strncpy(info.model_name, "Radeon HD / RX Graphics GPU", sizeof(info.model_name) - 1);
    } else {
      strncpy(info.vendor_name, "PCI Display Controller", sizeof(info.vendor_name) - 1);
      strncpy(info.model_name, "VGA / 3D Compatible Controller", sizeof(info.model_name) - 1);
    }
  } else {
    strncpy(info.vendor_name, "Generic Framebuffer", sizeof(info.vendor_name) - 1);
    strncpy(info.model_name, "VESA / EFI Linear Framebuffer", sizeof(info.model_name) - 1);
  }

  const fb_info_t *kfb = fb_get_info();
  if (kfb) {
    info.fb_width = kfb->width;
    info.fb_height = kfb->height;
    info.fb_bpp = kfb->bpp;
    if (info.vram_size_bytes == 0) {
      info.vram_size_bytes = (uint64_t)kfb->pitch * kfb->height;
    }
  }

  memcpy(user_gpu, &info, sizeof(gpu_info_t));
  regs->rax = 0;
}

void sys_sleep(struct registers *regs) {
  uint32_t ms = (uint32_t)regs->rdi;
  if (ms == 0) {
    schedule();
    regs->rax = 0;
    return;
  }
  uint64_t ticks = (ms + 9) / 10;
  thread_sleep(ticks);
  regs->rax = 0;
}

typedef struct pci_dev_info {
  uint8_t bus;
  uint8_t slot;
  uint8_t func;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t prog_if;
} __attribute__((packed)) pci_dev_info_t;

void sys_pci_list(struct registers *regs) {
  struct pci_dev_info *user_buf = (struct pci_dev_info *)regs->rdi;
  size_t max_entries = (size_t)regs->rsi;
  if (!user_buf || max_entries == 0) {
    regs->rax = (uint64_t)-EINVAL;
    return;
  }
  extern pci_device_node_t *pci_devices;
  pci_device_node_t *node = pci_devices;
  size_t count = 0;
  while (node && count < max_entries) {
    struct pci_dev_info info;
    info.bus = node->device.bus;
    info.slot = node->device.device;
    info.func = node->device.func;
    info.vendor_id = node->device.vendor_id;
    info.device_id = node->device.device_id;
    info.class_code = node->device.class_code;
    info.subclass = node->device.subclass;
    info.prog_if = node->device.prog_if;
    if (copy_to_user(&user_buf[count], &info, sizeof(info)) != 0) {
      regs->rax = (uint64_t)-EFAULT;
      return;
    }
    count++;
    node = node->next;
  }
  regs->rax = count;
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
  syscall_table[SYS_DISPLAY_CTL] = sys_display_ctl;
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
  // 60-70 POSIX extensions
  syscall_table[SYS_FORK] = sys_fork;
  syscall_table[SYS_WAITPID] = sys_waitpid;
  syscall_table[SYS_EXECVE] = sys_execve;
  syscall_table[SYS_PIPE] = sys_pipe;
  syscall_table[SYS_MMAP] = sys_mmap;
  syscall_table[SYS_MPROTECT] = sys_mprotect;
  syscall_table[SYS_MUNMAP] = sys_munmap;
  syscall_table[SYS_GETHOSTNAME] = sys_gethostname;
  syscall_table[SYS_SETHOSTNAME] = sys_sethostname;
  syscall_table[SYS_SIGACTION] = sys_sigaction;
  syscall_table[SYS_SIGPROCMASK] = sys_sigprocmask;
  syscall_table[SYS_GET_GPU_INFO] = sys_get_gpu_info;
  syscall_table[SYS_SLEEP] = sys_sleep;
  syscall_table[SYS_PCI_LIST] = sys_pci_list;
  klog(LOG_INFO, "Syscall handler expanded.");
}

// Registry consistency audit: every registry entry marked impl=yes/partial
// with a number < 256 must have a dispatch entry (except send which reuses
// the write path). Called late in boot (see kernel.c).
void syscall_audit_table() {
  int missing = 0;
  for (int i = 0; i < 256; i++) {
    if (syscall_table[i] == 0 && i <= SYS_PCI_LIST && i != SYS_SEND && i != SYS_DISPLAY_CTL) {
      if (i >= SYS_SENDTO && i <= SYS_PCI_LIST) missing++;
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
