#ifndef UACCESS_H
#define UACCESS_H

#include <stddef.h>
#include <stdint.h>

// Userspace memory access functions
int validate_user_pointer(const void *ptr, size_t size);
int copy_from_user(void *kernel_dst, const void *user_src, size_t size);
int copy_to_user(void *user_dst, const void *kernel_src, size_t size);

#endif // UACCESS_H
