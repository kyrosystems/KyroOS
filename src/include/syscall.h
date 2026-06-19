#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"

// Syscall numbers
#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_OPEN 2      // (const char *path, int flags)
#define SYS_CLOSE 3     // (int fd)
#define SYS_READ 4      // (int fd, void *buf, size_t size)
#define SYS_STAT 5      // (const char *path, struct stat *stat_buf)
#define SYS_MKDIR 6     // (const char *path)
#define SYS_READDIR 7   // (int fd, int index, struct dirent *dir_entry)
#define SYS_UNLINK 8    // (const char *path)
#define SYS_RMDIR 9     // (const char *path)
#define SYS_SOCKET 10   // (int domain, int type, int protocol)
#define SYS_CONNECT 11  // (int sockfd, const struct sockaddr_in *addr, size_t addrlen)
#define SYS_SEND 12     // (int sockfd, const void *buf, size_t len, int flags)
#define SYS_RECV 13     // (int sockfd, void *buf, size_t len, int flags)
#define SYS_BIND 14     // (int sockfd, const struct sockaddr_in *addr, size_t addrlen)
#define SYS_LISTEN 15   // (int sockfd, int backlog)
#define SYS_ACCEPT 16   // (int sockfd, struct sockaddr_in *addr, size_t *addrlen)
#define SYS_GET_TICKS 17
#define SYS_SHA256 18 // New: (const uint8_t* data, size_t len, uint8_t* hash_out)
#define SYS_MALLOC 19 // New: (size_t size)
#define SYS_FREE 20   // New: (void *ptr)
#define SYS_IOCTL 21 // New: (int fd, int request, void* argp)
#define SYS_MOUNT 22 // (const char* mount_point_path, const char* device_node_path, const char* fs_type_name)
#define SYS_UNMOUNT 23 // (const char* mount_point_path)
#define SYS_EXEC 24
#define SYS_GFX_GET_FB_INFO 25
#define SYS_INPUT_POLL_EVENT 26
#define SYS_SBRK 27
#define SYS_SENDTO      28
#define SYS_RECVFROM    29
#define SYS_SETSOCKOPT  30
#define SYS_DNS_RESOLVE 31

void syscall_init();
void syscall_handler(struct registers *regs);

#endif // SYSCALL_H
