#ifndef LOG_H
#define LOG_H

#include "isr.h" // For struct registers
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h> // For console_clear

// Set to 1 to enable early panic features (e.g., direct VGA text mode output during panic).
// Set to 0 to disable them, relying on a safer but less informative panic output.
#define CONFIG_PANIC_EARLY 1 

#define LOG_HISTORY_SIZE 10
#define LOG_MESSAGE_MAX_LEN 256

// Log levels
typedef enum {
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR,
  LOG_PANIC,
} log_level_t;

void log_init();
void klog(log_level_t level, const char *fmt, ...);
void panic(const char *message, struct registers *regs);

// Console functions for shell and other kernel modules
void klog_putchar(char c);
void klog_print_str(const char *s);
void console_clear(void);

// Expose console coordinates for direct manipulation by shell/editor
extern uint32_t console_x;
extern uint32_t console_y;

// Expose current log level for shell modification
extern log_level_t current_log_level;

// Early Serial Debug API
void serial_print(const char *str);
void serial_print_hex(uint64_t val);

// Retrieve the last `num_to_get` log entries.
// `entries` should be a buffer of size `num_to_get * LOG_MESSAGE_MAX_LEN`.
// `count` will be filled with the number of entries actually retrieved.
void log_get_entries(char* entries, int* count, int num_to_get);

#endif // LOG_H
