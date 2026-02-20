#ifndef KSTRING_H
#define KSTRING_H

#include <stdarg.h> // For va_list
#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strncpy(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

// GCC may optimize to these, so we provide them.
void *__memcpy_chk(void *dest, const void *src, size_t n, size_t dest_len);

// Basic sprintf implementation
int ksprintf(char *buffer, const char *format, ...);
int vksprintf(char *buffer, const char *format, va_list args);

#endif // KSTRING_H
