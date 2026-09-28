#ifndef KYROLIB_H
#define KYROLIB_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h> // For va_list, va_start, va_arg, va_end
#include <stdbool.h> // For bool type in sprintf

#include "vfs.h" // For O_* flags (e.g. O_RDONLY)
#include "event.h" // For event_t
#include "sysnum.h"
#include "procinfo.h" // shared kernel/userspace introspection structs
#include "socket.h" // For sockaddr_in

// Standard open flags (simplified)
#define O_RDONLY    0x0001 // Open for reading only
#define O_WRONLY    0x0002 // Open for writing only
#define O_RDWR      0x0003 // Open for reading and writing
#define O_CREAT     0x0040 // Create file if it does not exist
#define O_TRUNC     0x0200 // Truncate size to 0
#define O_APPEND    0x0400 // Append mode

#define SYS_SENDTO    28
#define SYS_RECVFROM  29

uint32_t dns_resolve(const char *host);

static inline uint64_t _syscall6(uint64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5,
                                uint64_t a6) {
  /* Pin every argument to its ABI register up front. Plain "r" inputs with
   * movq chains let the allocator pick rdi/rsi/rdx/rax for later operands
   * and earlier movqs then clobber them (classic wrapper bug). */
  register uint64_t r_num __asm__("rax") = num;
  register uint64_t r_a1  __asm__("rdi") = a1;
  register uint64_t r_a2  __asm__("rsi") = a2;
  register uint64_t r_a3  __asm__("rdx") = a3;
  register uint64_t r_a4  __asm__("r10") = a4;
  register uint64_t r_a5  __asm__("r8")  = a5;
  register uint64_t r_a6  __asm__("r9")  = a6;
  __asm__ __volatile__("int $0x80"
                       : "+a"(r_num)
                       : "r"(r_a1), "r"(r_a2), "r"(r_a3),
                         "r"(r_a4), "r"(r_a5), "r"(r_a6)
                       : "memory", "rcx", "r11");
  return r_num;
}

/* POSIX-style wrapper: kernel returns -errno; sets errno, returns -1. */
extern int errno;
void __set_errno(int err_code);
static inline long _sys(long num, long a1, long a2, long a3,
                        long a4, long a5, long a6) {
  long r = (long)_syscall6((uint64_t)num, (uint64_t)a1, (uint64_t)a2,
                           (uint64_t)a3, (uint64_t)a4, (uint64_t)a5, (uint64_t)a6);
  if (r < 0 && r >= -133) { __set_errno((int)-r); return -1; }
  return r;
}

static inline uint64_t syscall(uint64_t num, uint64_t a1, uint64_t a2,
                               uint64_t a3) {
  return _syscall6(num, a1, a2, a3, 0, 0, 0);
}

static inline void exit(int status) { syscall(SYS_EXIT, (uint64_t)status, 0, 0); }
void print(const char *s);
void print_u32(uint32_t n);
void print_u64(uint64_t n);
void print_hex(uint32_t n);

static inline int vsprintf(char *buffer, const char *format, va_list args) {
  char *buf_ptr = buffer;

  while (*format) {
    if (*format == '%') {
      format++;
      int padding = 0;
      char pad_char = ' ';

      if (*format == '0') {
        format++;
        if (*format >= '0' && *format <= '9') {
          padding = *format - '0';
          format++;
          if (*format >= '0' && *format <= '9') { 
            padding = padding * 10 + (*format - '0');
            format++;
          }
          pad_char = '0';
        }
      }

      bool is_long = false;
      bool is_long_long = false;
      if (*format == 'l') {
        is_long = true;
        format++;
        if (*format == 'l') {
          is_long_long = true;
          format++;
        }
      }

      switch (*format) {
      case 's': {
        char *s = va_arg(args, char *);
        if (!s)
          s = "(null)";
        while (*s) {
          *buf_ptr++ = *s++;
        }
        break;
      }
      case 'c': {
        char c = (char)va_arg(args, int);
        *buf_ptr++ = c;
        break;
      }
      case 'p':
      case 'x': {
        uint64_t val;
        if (*format == 'p') {
          val = (uint64_t)va_arg(args, void *);
          *buf_ptr++ = '0';
          *buf_ptr++ = 'x';
        } else {
          if (is_long_long || is_long) {
            val = va_arg(args, uint64_t);
          } else {
            val = va_arg(args, uint32_t);
          }
        }

        char hex_chars[] = "0123456789abcdef";
        char hex_buf[20];
        int i = 0;
        if (val == 0) {
          hex_buf[i++] = '0';
        } else {
          while (val > 0) {
            hex_buf[i++] = hex_chars[val % 16];
            val /= 16;
          }
        }

        int h_padding = padding > i ? padding : i;
        for (int j = 0; j < h_padding - i; j++) {
          *buf_ptr++ = pad_char;
        }
        while (i > 0) {
          *buf_ptr++ = hex_buf[--i];
        }
        break;
      }
      case 'u':
      case 'd': {
        uint64_t val;
        if (is_long_long || is_long) {
          val = va_arg(args, uint64_t);
        } else {
          val = (uint64_t)va_arg(args, int);
        }

        if (*format == 'd' && (int64_t)val < 0) {
          *buf_ptr++ = '-';
          val = -(int64_t)val;
        }

        char num_str[21]; 
        int i = 0;
        if (val == 0) {
          num_str[i++] = '0';
        } else {
          while (val > 0) {
            num_str[i++] = (val % 10) + '0';
            val /= 10;
          }
        }

        int d_padding = padding > i ? padding : i;
        for (int j = 0; j < d_padding - i; j++) {
          *buf_ptr++ = pad_char;
        }
        while (i > 0) {
          *buf_ptr++ = num_str[--i];
        }
        break;
      }
      default:
        if (pad_char == '0' && padding > 0) { 
          *buf_ptr++ = '%';
          *buf_ptr++ = '0';
          if (padding >= 10)
            *buf_ptr++ = (padding / 10) + '0';
          *buf_ptr++ = (padding % 10) + '0';
        } else { 
          *buf_ptr++ = '%';
        }
        *buf_ptr++ = *format;
        break;
      }
    } else {
      *buf_ptr++ = *format;
    }
    format++;
  }
  *buf_ptr = '\0';
  return buf_ptr - buffer;
}

static inline int sprintf(char *buffer, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int ret = vsprintf(buffer, format, args);
  va_end(args);
  return ret;
}

static inline int open(const char *path, int flags) {
  return (int)syscall(SYS_OPEN, (uint64_t)path, (uint64_t)flags, 0);
}
static inline int close(int fd) {
  return (int)syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
}
static inline int read(int fd, void *buf, size_t size) {
  return (int)syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)size);
}

static inline int write(int fd, const void *buf, size_t size) {
  return (int)syscall(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)size);
}
static inline int mkdir(const char *path) {
  return (int)syscall(SYS_MKDIR, (uint64_t)path, 0, 0);
}
static inline int stat(const char *path, struct stat *stat_buf) {
  return (int)syscall(SYS_STAT, (uint64_t)path, (uint64_t)stat_buf, 0);
}
static inline int unlink(const char *path) {
  return (int)syscall(SYS_UNLINK, (uint64_t)path, 0, 0);
}
static inline int rmdir(const char *path) {
  return (int)syscall(SYS_RMDIR, (uint64_t)path, 0, 0);
}
static inline int readdir(int fd, int index, struct dirent *dir_entry) {
  return (int)syscall(SYS_READDIR, (uint64_t)fd, (uint64_t)index, (uint64_t)dir_entry);
}
static inline int create(const char *path) {
    return open(path, O_CREAT | O_TRUNC | O_WRONLY);
}
static inline int exec(const char *path, char **argv, int argc) {
    return (int)syscall(SYS_EXEC, (uint64_t)path, (uint64_t)argv, (uint64_t)argc);
}

// New socket wrappers
static inline int socket(int domain, int type, int protocol) {
  return (int)syscall(SYS_SOCKET, (uint64_t)domain, (uint64_t)type, (uint64_t)protocol);
}
static inline int bind(int sockfd, const struct sockaddr_in *addr, size_t addrlen) {
  return (int)syscall(SYS_BIND, (uint64_t)sockfd, (uint64_t)addr, (uint64_t)addrlen);
}
static inline int listen(int sockfd, int backlog) {
  return (int)syscall(SYS_LISTEN, (uint64_t)sockfd, (uint64_t)backlog, 0);
}
static inline int accept(int sockfd, struct sockaddr_in *addr, size_t *addrlen) {
  return (int)syscall(SYS_ACCEPT, (uint64_t)sockfd, (uint64_t)addr, (uint64_t)addrlen);
}
static inline int connect(int sockfd, const struct sockaddr_in *addr, size_t addrlen) {
  return (int)syscall(SYS_CONNECT, (uint64_t)sockfd, (uint64_t)addr, (uint64_t)addrlen);
}

// Host to network byte order conversion for 16-bit unsigned short
static inline uint16_t htons(uint16_t val) {
    return (val << 8) | (val >> 8);
}
static inline int send(int sockfd, const void *buf, size_t len, int /*flags*/) {
    return syscall(SYS_SEND, sockfd, (uint64_t)buf, len);
}
static inline int recv(int sockfd, void *buf, size_t len, int /*flags*/) {
    return syscall(SYS_RECV, sockfd, (uint64_t)buf, len);
}

static inline int sendto(int sockfd, const void *buf, size_t len, int flags,
                         const struct sockaddr_in *addr, socklen_t addrlen) {
    return (int)_sys(SYS_SENDTO, sockfd, (long)buf, len, flags, (long)addr, addrlen);
}

static inline int recvfrom(int sockfd, void *buf, size_t len, int flags,
                            struct sockaddr_in *addr, socklen_t *addrlen) {
    return (int)_sys(SYS_RECVFROM, sockfd, (long)buf, len, flags, (long)addr, (long)addrlen);
}

static inline int setsockopt(int sockfd, int level, int optname,
                              const void *optval, socklen_t optlen) {
    return (int)_sys(SYS_SETSOCKOPT, sockfd, level, optname, (long)optval, optlen, 0);
}


static inline int sha256(const uint8_t* data, size_t len, uint8_t* hash_out) {
  return (int)syscall(SYS_SHA256, (uint64_t)data, (uint64_t)len, (uint64_t)hash_out);
}

static int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    void* buffer;
} user_fb_info_t;

static inline int gfx_get_fb_info(user_fb_info_t *info_ptr) {
    return (int)syscall(SYS_GFX_GET_FB_INFO, (uint64_t)info_ptr, 0, 0);
}

static inline int ioctl(int fd, int request, void* argp) {
    return (int)syscall(SYS_IOCTL, (uint64_t)fd, (uint64_t)request, (uint64_t)argp);
}

static inline int mount(const char* mount_point_path, const char* device_node_path, const char* fs_type_name) {
    return (int)syscall(SYS_MOUNT, (uint64_t)mount_point_path, (uint64_t)device_node_path, (uint64_t)fs_type_name);
}

static inline int unmount(const char* mount_point_path) {
    return (int)syscall(SYS_UNMOUNT, (uint64_t)mount_point_path, 0, 0);
}

static inline int input_poll_event(event_t *event) {
    return (int)syscall(SYS_INPUT_POLL_EVENT, (uint64_t)event, 0, 0);
}

int atoi(const char *s);

static inline uint64_t get_ticks() { return syscall(SYS_GET_TICKS, 0, 0, 0); }


/* Timer runs at 100 Hz (see kernel.c timer_init(100)): 1 tick = 10 ms. */
static inline uint64_t uptime_ticks(void) {
    return get_ticks();
}
static inline uint32_t uptime_ms(void) {
    return (uint32_t)(get_ticks() * 10u);
}

static inline void sleep_ms(uint32_t ms) {
    uint32_t end = uptime_ms() + ms;
    while (uptime_ms() < end) {}
}

// Minimal string/memory functions
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int memcmp(const void *s1, const void *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
char *strrchr(const char *s, int c);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strncpy(char *dest, const char *src, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int atoi(const char *s);

static inline void *malloc(size_t size) {
    return (void*)syscall(SYS_MALLOC, (uint64_t)size, 0, 0);
}

static inline void free(void *ptr) {
    syscall(SYS_FREE, (uint64_t)ptr, 0, 0);
}

static inline void *sbrk(intptr_t increment) {
    return (void *)syscall(SYS_SBRK, (uint64_t)increment, 0, 0);
}

// ---- New ABI wrappers (errno-contract, 6-arg capable) ----
static inline int debugmode_get(void) {
  return (int)_syscall6(SYS_DEBUGMODE_GET, 0, 0, 0, 0, 0, 0);
}
static inline int debugmode_set(int mode) {
  return (int)_sys(SYS_DEBUGMODE_SET, mode, 0, 0, 0, 0, 0);
}
static inline uint32_t getuid(void) {
  return (uint32_t)_syscall6(SYS_GETUID, 0, 0, 0, 0, 0, 0);
}
static inline uint32_t geteuid(void) {
  return (uint32_t)_syscall6(SYS_GETEUID, 0, 0, 0, 0, 0, 0);
}
static inline uint32_t getgid(void) {
  return (uint32_t)_syscall6(SYS_GETGID, 0, 0, 0, 0, 0, 0);
}
static inline uint32_t getegid(void) {
  return (uint32_t)_syscall6(SYS_GETEGID, 0, 0, 0, 0, 0, 0);
}
static inline int setuid(uint32_t uid) {
  return (int)_sys(SYS_SETUID, uid, 0, 0, 0, 0, 0);
}
static inline int setgid(uint32_t gid) {
  return (int)_sys(SYS_SETGID, gid, 0, 0, 0, 0, 0);
}
static inline int seteuid(uint32_t euid) {
  return (int)_sys(SYS_SETEUID, euid, 0, 0, 0, 0, 0);
}
static inline int setegid(uint32_t egid) {
  return (int)_sys(SYS_SETEGID, egid, 0, 0, 0, 0, 0);
}
static inline uint32_t umask(uint32_t m) {
  return (uint32_t)_syscall6(SYS_UMASK, m, 0, 0, 0, 0, 0);
}
static inline int chmod(const char *path, uint32_t mode) {
  return (int)_sys(SYS_CHMOD, (long)path, mode, 0, 0, 0, 0);
}
static inline int fchmod(int fd, uint32_t mode) {
  return (int)_sys(SYS_FCHMOD, fd, mode, 0, 0, 0, 0);
}
static inline int chown(const char *path, uint32_t uid, uint32_t gid) {
  return (int)_sys(SYS_CHOWN, (long)path, uid, gid, 0, 0, 0);
}
static inline int getcwd(char *buf, size_t size) {
  return (int)_syscall6(SYS_GETCWD, (uint64_t)buf, size, 0, 0, 0, 0);
}
static inline int chdir(const char *path) {
  return (int)_sys(SYS_CHDIR, (long)path, 0, 0, 0, 0, 0);
}
static inline uint32_t getpid(void) {
  return (uint32_t)_syscall6(SYS_GETPID, 0, 0, 0, 0, 0, 0);
}
static inline int kill(uint32_t pid, int sig) {
  return (int)_sys(SYS_KILL, pid, sig, 0, 0, 0, 0);
}
static inline int dup(int fd) {
  return (int)_sys(SYS_DUP, fd, 0, 0, 0, 0, 0);
}
static inline int dup2(int fd, int fd2) {
  return (int)_sys(SYS_DUP2, fd, fd2, 0, 0, 0, 0);
}
static inline int lseek(int fd, int64_t offset, int whence) {
  return (int)_sys(SYS_LSEEK, fd, offset, whence, 0, 0, 0);
}
static inline int fstat(int fd, struct stat *st) {
  return (int)_sys(SYS_FSTAT, fd, (long)st, 0, 0, 0, 0);
}
static inline int shutdown(int fd, int how) {
  return (int)_sys(SYS_SOCK_SHUTDOWN, fd, how, 0, 0, 0, 0);
}
static inline int getsockname(int fd, struct sockaddr_in *addr, socklen_t *addrlen) {
  return (int)_sys(SYS_GETSOCKNAME, fd, (long)addr, (long)addrlen, 0, 0, 0);
}

// Introspection (KyroOS-specific syscalls)
static inline int proc_list(struct procinfo *buf, size_t max_entries) {
  return (int)_sys(SYS_PROC_LIST, (long)buf, (long)max_entries, 0, 0, 0, 0);
}
static inline int sysinfo_k(struct sysinfo_k *out) {
  return (int)_sys(SYS_SYSINFO, (long)out, 0, 0, 0, 0, 0);
}
static inline int net_ifconf(struct ifinfo *buf, size_t max_entries) {
  return (int)_sys(SYS_NET_IFCONF, (long)buf, (long)max_entries, 0, 0, 0, 0);
}

#endif // KYROLIB_H
