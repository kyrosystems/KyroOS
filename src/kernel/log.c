#include "log.h"
#include "epstein.h"
#include "fb.h"
#include "font.h"
#include "isr.h"
#include "kstring.h"
#include "port_io.h"
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>

#define FB_DEFAULT_FG_COLOR 0xFFFFFFFF
#define FB_DEFAULT_BG_COLOR 0x00000000
#define COM1 0x3F8

uint32_t console_x = 0;
uint32_t console_y = 0;
static uint32_t console_cols = 0;
static uint32_t console_rows = 0;
static const uint32_t char_width = 8;
static const uint32_t char_height = 16;
static uint32_t current_fg_color = FB_DEFAULT_FG_COLOR;
static uint32_t current_bg_color = FB_DEFAULT_BG_COLOR;
static bool sgr_invert = false;
static bool cursor_visible = true;
static bool cursor_drawn = false;
static uint32_t cursor_saved_x = 0;
static uint32_t cursor_saved_y = 0;
static uint32_t cursor_saved_pixels[16];
log_level_t current_log_level = LOG_INFO;

void log_update_console_dimensions(void) {
  const fb_info_t *info = fb_get_info();
  if (info) {
    console_cols = info->width / char_width;
    console_rows = info->height / char_height;
  }
  console_clear();
}

static void serial_putchar(char c) {
  while ((inb(COM1 + 5) & 0x20) == 0) {}
  outb(COM1, c);
}

void serial_print(const char *s) {
  while (*s) serial_putchar(*s++);
}

void serial_print_hex(uint64_t value) {
  static const char digits[] = "0123456789abcdef";
  serial_print("0x");
  for (int shift = 60; shift >= 0; shift -= 4)
    serial_putchar(digits[(value >> shift) & 15]);
}

void log_set_fg_color(uint32_t color) {
  current_fg_color = color;
}

void klog_draw_cursor(void) {
  if (!cursor_visible || cursor_drawn) return;
  if (console_cols == 0 || console_rows == 0) return;
  if (console_x >= console_cols || console_y >= console_rows) return;

  uint32_t px = console_x * char_width;
  uint32_t py = console_y * char_height;
  for (uint32_t dy = 0; dy < 2; dy++) {
    for (uint32_t dx = 0; dx < char_width; dx++) {
      cursor_saved_pixels[dy * char_width + dx] = fb_get_pixel(px + dx, py + char_height - 2 + dy);
      fb_set_pixel(px + dx, py + char_height - 2 + dy, 0xFFFFFFFF);
    }
  }
  cursor_saved_x = console_x;
  cursor_saved_y = console_y;
  cursor_drawn = true;
  fb_flush_dirty();
}

void klog_clear_cursor(void) {
  if (!cursor_drawn) return;
  uint32_t px = cursor_saved_x * char_width;
  uint32_t py = cursor_saved_y * char_height;
  for (uint32_t dy = 0; dy < 2; dy++) {
    for (uint32_t dx = 0; dx < char_width; dx++) {
      fb_set_pixel(px + dx, py + char_height - 2 + dy, cursor_saved_pixels[dy * char_width + dx]);
    }
  }
  cursor_drawn = false;
  fb_flush_dirty();
}

void klog_clear_line(void) {
  klog_clear_cursor();
  uint32_t bg = sgr_invert ? current_fg_color : current_bg_color;
  fb_draw_rect(0, console_y * char_height, console_cols * char_width, char_height, bg);
  console_x = 0;
  klog_draw_cursor();
}

/* Comprehensive ANSI CSI parser for terminal TUIs (nano, edit, sh, sysmon):
 * Handles cursor positioning \033[row;colH, cursor movement \033[A/B/C/D,
 * line/screen clearing \033[K / \033[2J, SGR colors & inversion \033[m,
 * and cursor show/hide \033[?25h / \033[?25l. */
static int ansi_state = 0; /* 0=text, 1=seen ESC, 2=in CSI */
#define MAX_CSI_PARAMS 8
static int csi_params[MAX_CSI_PARAMS];
static bool csi_has_param[MAX_CSI_PARAMS];
static int csi_param_count = 0;
static bool csi_is_private = false;

static void ansi_consume(char c) {
  if (ansi_state == 1) {
    if (c == '[') {
      ansi_state = 2;
      csi_param_count = 0;
      csi_params[0] = 0;
      csi_has_param[0] = false;
      csi_is_private = false;
      return;
    }
    ansi_state = 0;
    if (c == 0x1B) ansi_state = 1;
    return;
  }

  /* ansi_state == 2: collecting CSI */
  if (c == '?') {
    csi_is_private = true;
    return;
  }

  if (c >= '0' && c <= '9') {
    csi_params[csi_param_count] = csi_params[csi_param_count] * 10 + (c - '0');
    csi_has_param[csi_param_count] = true;
    return;
  }

  if (c == ';') {
    if (csi_param_count < MAX_CSI_PARAMS - 1) {
      csi_param_count++;
      csi_params[csi_param_count] = 0;
      csi_has_param[csi_param_count] = false;
    }
    return;
  }

  /* Final byte (@ through ~) terminates CSI */
  if (c >= '@' && c <= '~') {
    klog_clear_cursor();
    if (c == 'H' || c == 'f') { // Cursor Position: \033[row;colH
      int row = (csi_has_param[0] && csi_params[0] > 0) ? csi_params[0] : 1;
      int col = (csi_param_count >= 1 && csi_has_param[1] && csi_params[1] > 0) ? csi_params[1] : 1;
      console_y = (uint32_t)(row > 0 ? row - 1 : 0);
      console_x = (uint32_t)(col > 0 ? col - 1 : 0);
      if (console_cols && console_x >= console_cols) console_x = console_cols - 1;
      if (console_rows && console_y >= console_rows) console_y = console_rows - 1;
    } else if (c == 'A') { // Cursor Up: \033[<n>A
      int n = (csi_has_param[0] && csi_params[0] > 0) ? csi_params[0] : 1;
      console_y = console_y >= (uint32_t)n ? console_y - n : 0;
    } else if (c == 'B') { // Cursor Down: \033[<n>B
      int n = (csi_has_param[0] && csi_params[0] > 0) ? csi_params[0] : 1;
      console_y = (console_y + n < console_rows) ? console_y + n : (console_rows ? console_rows - 1 : 0);
    } else if (c == 'C') { // Cursor Forward (Right): \033[<n>C
      int n = (csi_has_param[0] && csi_params[0] > 0) ? csi_params[0] : 1;
      console_x = (console_x + n < console_cols) ? console_x + n : (console_cols ? console_cols - 1 : 0);
    } else if (c == 'D') { // Cursor Back (Left): \033[<n>D
      int n = (csi_has_param[0] && csi_params[0] > 0) ? csi_params[0] : 1;
      console_x = console_x >= (uint32_t)n ? console_x - n : 0;
    } else if (c == 'J') { // Erase in Display: \033[2J
      int mode = csi_has_param[0] ? csi_params[0] : 0;
      if (mode == 2 || mode == 3) {
        console_clear();
      } else if (mode == 0) {
        uint32_t bg = sgr_invert ? current_fg_color : current_bg_color;
        if (console_x < console_cols) {
          fb_draw_rect(console_x * char_width, console_y * char_height,
                       (console_cols - console_x) * char_width, char_height, bg);
        }
        if (console_y + 1 < console_rows) {
          fb_draw_rect(0, (console_y + 1) * char_height,
                       console_cols * char_width, (console_rows - (console_y + 1)) * char_height, bg);
        }
      }
    } else if (c == 'K') { // Erase in Line: \033[K, \033[2K
      int mode = csi_has_param[0] ? csi_params[0] : 0;
      uint32_t bg = sgr_invert ? current_fg_color : current_bg_color;
      if (mode == 0) {
        if (console_x < console_cols) {
          fb_draw_rect(console_x * char_width, console_y * char_height,
                       (console_cols - console_x) * char_width, char_height, bg);
        }
      } else if (mode == 1) {
        uint32_t w = (console_x + 1 < console_cols) ? (console_x + 1) : console_cols;
        fb_draw_rect(0, console_y * char_height, w * char_width, char_height, bg);
      } else if (mode == 2) {
        fb_draw_rect(0, console_y * char_height, console_cols * char_width, char_height, bg);
      }
    } else if (c == 'm') { // SGR attributes: \033[m
      int total = csi_has_param[0] ? (csi_param_count + 1) : 1;
      for (int i = 0; i < total; i++) {
        int val = csi_has_param[i] ? csi_params[i] : 0;
        if (val == 0) {
          current_fg_color = FB_DEFAULT_FG_COLOR;
          current_bg_color = FB_DEFAULT_BG_COLOR;
          sgr_invert = false;
        } else if (val == 7) {
          sgr_invert = true;
        } else if (val == 27) {
          sgr_invert = false;
        } else if (val >= 30 && val <= 37) {
          static const uint32_t ansi_colors[8] = {
            0xFF000000, 0xFFCC0000, 0xFF00CC00, 0xFFCCCC00,
            0xFF2255CC, 0xFFCC00CC, 0xFF00CCCC, 0xFFAAAAAA
          };
          current_fg_color = ansi_colors[val - 30];
        } else if (val == 39) {
          current_fg_color = FB_DEFAULT_FG_COLOR;
        } else if (val >= 40 && val <= 47) {
          static const uint32_t ansi_bg[8] = {
            0xFF000000, 0xFF880000, 0xFF008800, 0xFF888800,
            0xFF000088, 0xFF880088, 0xFF008888, 0xFF888888
          };
          current_bg_color = ansi_bg[val - 40];
        } else if (val == 49) {
          current_bg_color = FB_DEFAULT_BG_COLOR;
        } else if (val >= 90 && val <= 97) {
          static const uint32_t ansi_bright[8] = {
            0xFF555555, 0xFFFF4444, 0xFF44FF44, 0xFFFFFF44,
            0xFF4488FF, 0xFFFF44FF, 0xFF44FFFF, 0xFFFFFFFF
          };
          current_fg_color = ansi_bright[val - 90];
        } else if (val >= 100 && val <= 107) {
          static const uint32_t ansi_bright_bg[8] = {
            0xFF333333, 0xFFAA2222, 0xFF22AA22, 0xFFAAAA22,
            0xFF2222AA, 0xFFAA22AA, 0xFF22AAAA, 0xFFAAAAAA
          };
          current_bg_color = ansi_bright_bg[val - 100];
        }
      }
    } else if (c == 'h' || c == 'l') { // Set / Reset mode
      if (csi_is_private && csi_has_param[0] && csi_params[0] == 25) {
        cursor_visible = (c == 'h');
        if (!cursor_visible) klog_clear_cursor();
      }
    }
    ansi_state = 0;
    return;
  }
}

void klog_putchar(char c) {
  if (ansi_state != 0) { ansi_consume(c); return; }

  if (c == 0x1B) {
    ansi_state = 1;
    return;
  }

  klog_clear_cursor();

  if (c == '\n') {
    console_x = 0;
    console_y++;
  } else if (c == '\r') {
    console_x = 0;
  } else if (c == '\b') {
    if (console_x > 0) {
      console_x--;
      uint32_t bg = sgr_invert ? current_fg_color : current_bg_color;
      fb_draw_rect(console_x * char_width, console_y * char_height,
                   char_width, char_height, bg);
    }
  } else {
    uint32_t eff_fg = sgr_invert ? current_bg_color : current_fg_color;
    uint32_t eff_bg = sgr_invert ? current_fg_color : current_bg_color;
    fb_draw_char(c, console_x * char_width, console_y * char_height,
                 eff_fg, eff_bg);
    console_x++;
  }

  if (console_cols > 0 && console_x >= console_cols) {
    console_x = 0;
    console_y++;
  }
  if (console_rows > 0 && console_y >= console_rows) {
    fb_copy_region(char_height, 0, (console_rows - 1) * char_height);
    uint32_t bg = sgr_invert ? current_fg_color : current_bg_color;
    fb_draw_rect(0, (console_rows - 1) * char_height,
                 console_cols * char_width, char_height, bg);
    console_y = console_rows - 1;
  }
}

void klog_print_str(const char *s, bool do_flush) {
  while (*s) klog_putchar(*s++);
  if (do_flush) {
    klog_draw_cursor();
    fb_flush_dirty();
  }
}

void console_clear(void) {
  klog_clear_cursor();
  uint32_t bg = sgr_invert ? current_fg_color : current_bg_color;
  fb_clear(bg);
  console_x = 0;
  console_y = 0;
  fb_flush();
  klog_draw_cursor();
}

void log_init(void) {
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x80);
  outb(COM1 + 0, 0x03);
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x03);
  outb(COM1 + 2, 0xC7);
  outb(COM1 + 4, 0x0B);
}

void klog(log_level_t level, const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vksprintf(buf, fmt, args);
  va_end(args);
  serial_print(buf);
  serial_print("\n");

  if (level == LOG_DEBUG) return;
  if (epstein == 1 || level >= LOG_WARN) {
    klog_print_str(buf, false);
    klog_putchar('\n');
    fb_flush_dirty();
  }
}

void log_get_entries(char *entries, int *count, int num_to_get) {
  if (count) *count = 0;
  (void)entries;
  (void)num_to_get;
}

void panic(const char *message, struct registers *regs) {
  __asm__ __volatile__("cli");
  (void)regs;
  fb_set_graphics_owner(0);
  fb_clear(0xFF770000);
  console_x = 0;
  console_y = 0;
  klog_print_str("PANIC: ", false);
  klog_print_str(message, true);
  for (;;) __asm__ __volatile__("hlt");
}

void log_dump(void) {}
