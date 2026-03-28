#include "log.h"
#include <stdint.h>
#include <stdbool.h>
#include "font.h"
#include "port_io.h"
#include "fb.h"
#include "kstring.h"
#include "isr.h"
#include <stdarg.h>

#define FB_DEFAULT_FG_COLOR 0xFFFFFFFF
#define FB_DEFAULT_BG_COLOR 0x00000000 
#define COM1 0x3F8

uint32_t console_x = 0;
uint32_t console_y = 0;
static uint32_t console_cols = 0;
static uint32_t console_rows = 0;
static const uint32_t char_width = 8;
static const uint32_t char_height = 16;

log_level_t current_log_level = LOG_INFO;

void log_update_console_dimensions() {
  const fb_info_t *fb_info = fb_get_info();
  if (fb_info) {
      console_cols = fb_info->width / char_width;
      console_rows = fb_info->height / char_height;
  }
  console_clear(); 
}

static void serial_putchar(char c) {
  while ((inb(COM1 + 5) & 0x20) == 0);
  outb(COM1, c);
}

void serial_print(const char *s) { while (*s) serial_putchar(*s++); }

static uint32_t current_fg_color = FB_DEFAULT_FG_COLOR;

void log_set_fg_color(uint32_t color) {
    current_fg_color = color;
}

void klog_putchar(char c) {
  if (c == '\n') {
    console_x = 0;
    console_y++;
  } else if (c == '\r') {
    console_x = 0;
  } else if (c == '\b') { 
    if (console_x > 0) {
        console_x--;
        fb_draw_rect(console_x * char_width, console_y * char_height, char_width, char_height, FB_DEFAULT_BG_COLOR);
    }
  } else {
    fb_draw_char(c, console_x * char_width, console_y * char_height, current_fg_color, FB_DEFAULT_BG_COLOR);
    console_x++;
  }

  if (console_x >= console_cols) { console_x = 0; console_y++; }

  if (console_rows > 0 && console_y >= console_rows) {
    fb_copy_region(char_height, 0, (console_rows - 1) * char_height);
    fb_draw_rect(0, (console_rows - 1) * char_height, console_cols * char_width, char_height, FB_DEFAULT_BG_COLOR);
    console_y = console_rows - 1;
  }
}

void klog_print_str(const char *s, bool do_flush) {
  while (*s) klog_putchar(*s++);
  if (do_flush) fb_flush();
}

void console_clear(void) { fb_clear(FB_DEFAULT_BG_COLOR); console_x = 0; console_y = 0; fb_flush(); }

void log_init() {
  outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x80); outb(COM1 + 0, 0x03); outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xC7); outb(COM1 + 4, 0x0B); 
}

void klog(log_level_t level, const char *fmt, ...) {
  (void)level;
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vksprintf(buf, fmt, args);
  va_end(args);
  serial_print(buf); serial_print("\n");
  klog_print_str(buf, false);
  klog_putchar('\n');
  fb_flush();
}

void log_get_entries(char *entries, int *count, int num_to_get) {
    if (count) *count = 0; (void)entries; (void)num_to_get;
}

void panic(const char *message, struct registers *regs) {
    __asm__ __volatile__("cli");
    (void)regs;
    fb_clear(0xFF770000); 
    console_x = 0; console_y = 0;
    klog_print_str("PANIC: ", false); klog_print_str(message, true);
    for (;;) { __asm__ __volatile__("hlt"); }
}
void serial_print_hex(uint64_t n) { (void)n; }
void klog_draw_cursor() {}
void klog_clear_cursor() {}
void klog_clear_line() {}
void log_dump() {}
