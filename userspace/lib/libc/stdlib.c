#include <kyroolib.h>

// Simple atoi implementation
int atoi(const char *str) {
  int result = 0;
  int sign = 1;

  // Skip whitespace
  while (*str == ' ' || *str == '\t' || *str == '\n') {
    str++;
  }

  // Handle sign
  if (*str == '-') {
    sign = -1;
    str++;
  } else if (*str == '+') {
    str++;
  }

  // Convert digits
  while (*str >= '0' && *str <= '9') {
    result = result * 10 + (*str - '0');
    str++;
  }

  return result * sign;
}

// snprintf-like function (simplified)
int snprintf(char *buf, size_t size, const char *fmt, ...) {
  if (size == 0)
    return 0;

  va_list args;
  va_start(args, fmt);

  // Use ksprintf but limit to size
  char temp_buf[2048];
  int len = ksprintf(temp_buf, fmt, args);

  va_end(args);

  // Copy to actual buffer with size limit
  size_t copy_len = ((size_t)len < size - 1) ? (size_t)len : (size - 1);
  for (size_t i = 0; i < copy_len; i++) {
    buf[i] = temp_buf[i];
  }
  buf[copy_len] = '\0';

  return len;
}
