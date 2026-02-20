#include <kyroolib.h> // For write syscall
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>


// Simple implementation of print - writes to stdout (fd 1)
void print(const char *str) {
  if (!str)
    return;

  size_t len = 0;
  while (str[len])
    len++;

  write(1, str, len);
}

// Simple implementation of ksprintf (like sprintf but simpler)
int ksprintf(char *buf, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  char *dest = buf;
  const char *src = fmt;

  while (*src) {
    if (*src == '%') {
      src++;
      if (*src == 's') {
        // String argument
        const char *str = va_arg(args, const char *);
        while (*str) {
          *dest++ = *str++;
        }
      } else if (*src == 'd') {
        // Integer argument
        int num = va_arg(args, int);
        if (num < 0) {
          *dest++ = '-';
          num = -num;
        }

        char temp[32];
        int i = 0;
        do {
          temp[i++] = '0' + (num % 10);
          num /= 10;
        } while (num > 0);

        while (i > 0) {
          *dest++ = temp[--i];
        }
      } else if (*src == 'x' || *src == 'p') {
        // Hex argument
        unsigned long num = va_arg(args, unsigned long);
        if (*src == 'p') {
          *dest++ = '0';
          *dest++ = 'x';
        }

        char temp[32];
        int i = 0;
        do {
          int digit = num % 16;
          temp[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
          num /= 16;
        } while (num > 0);

        if (i == 0)
          temp[i++] = '0';

        while (i > 0) {
          *dest++ = temp[--i];
        }
      } else if (*src == '%') {
        *dest++ = '%';
      }
      src++;
    } else {
      *dest++ = *src++;
    }
  }

  *dest = '\0';
  va_end(args);
  return dest - buf;
}
