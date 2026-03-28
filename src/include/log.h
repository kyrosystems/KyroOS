#ifndef LOG_H
#define LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "isr.h"

typedef enum {
  LOG_DEBUG = 0,
  LOG_INFO = 1,
  LOG_WARN = 2,
  LOG_ERROR = 3,
  LOG_PANIC = 4
} log_level_t;

#define LOG_HISTORY_SIZE 128
#define LOG_MESSAGE_MAX_LEN 128

extern log_level_t current_log_level;
extern uint32_t console_x;
extern uint32_t console_y;

void log_init();
void log_update_console_dimensions();
void klog(log_level_t level, const char *fmt, ...);
void klog_print_str(const char *s, bool do_flush);
void klog_putchar(char c);
void log_set_fg_color(uint32_t color);
void klog_draw_cursor();
void klog_clear_cursor();
void klog_clear_line();
void console_clear();
void panic(const char *message, struct registers *regs);

void serial_print(const char *s);
void serial_print_hex(uint64_t n);

void log_dump();
void log_get_entries(char *entries, int *count, int num_to_get);

#endif
