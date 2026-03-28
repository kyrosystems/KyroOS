#include "log.h"
#include "epstein.h"
#include "fb.h"      // For framebuffer operations
#include "font.h"    // For font dimensions and glyphs
#include "isr.h"     // Include for timer_get_ticks
#include "kstring.h" // For vksprintf
#include "panic_screen.h"
#include "port_io.h"
#include "version.h"
#include <stdarg.h> // For va_list in klog
#include <stddef.h>
#include <stdint.h>

// Default colors for framebuffer output
#define FB_DEFAULT_FG_COLOR 0xFFFFFFFF // White
#define FB_DEFAULT_BG_COLOR 0xFF00004A // Dark Blue

// Serial port constants
#define COM1 0x3F8

uint32_t console_x = 0;
uint32_t console_y = 0;

log_level_t console_log_level = LOG_WARN;
log_level_t serial_log_level = LOG_DEBUG;

// Circular buffer for recent log messages
static char log_history[LOG_HISTORY_SIZE][LOG_MESSAGE_MAX_LEN];
static uint32_t log_history_idx = 0;
static bool log_history_full = false;

// --- Serial Functions ---

static void serial_init() {
  outb(COM1 + 1, 0x00); // Disable all interrupts
  outb(COM1 + 3, 0x80); // Enable DLAB (set baud rate divisor)
  outb(COM1 + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
  outb(COM1 + 1, 0x00); //                  (hi byte)
  outb(COM1 + 3, 0x03); // 8 bits, no parity, one stop bit
  outb(COM1 + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
  outb(COM1 + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

static void serial_putchar(char c) {
  while ((inb(COM1 + 5) & 0x20) == 0)
    ; // Wait for transmitter to be empty
  outb(COM1, c);
}

void serial_print(const char *s) {
  while (*s) {
    serial_putchar(*s++);
  }
}

void serial_print_hex(uint64_t n) {
  char hex[] = "0123456789abcdef";
  serial_print("0x");
  for (int i = 60; i >= 0; i -= 4) {
    serial_putchar(hex[(n >> i) & 0xf]);
  }
}

// --- Framebuffer Text Output Functions ---

void klog_putchar(char c) {
  serial_putchar(c); // Always redirect to serial

  const fb_info_t *fb_info = fb_get_info();
  if (!fb_info || !fb_is_backbuffer_initialized()) {
    return; // Cannot draw to framebuffer if not initialized
  }

  // Calculate maximum characters per line and lines per screen
  uint32_t chars_per_line = fb_info->width / FONT_WIDTH;
  // uint32_t lines_per_screen = fb_info->height / FONT_HEIGHT; // Unused, hence the warning

  if (c == '\n') {
    console_x = 0;
    console_y += FONT_HEIGHT;
  } else if (c == '\b') { // Handle backspace
    if (console_x > 0) {
      console_x -= FONT_WIDTH;
      fb_draw_char(' ', console_x, console_y, FB_DEFAULT_FG_COLOR, FB_DEFAULT_BG_COLOR);
    } else if (console_y > 0) {
      console_y -= FONT_HEIGHT;
      console_x = (chars_per_line - 1) * FONT_WIDTH;
      fb_draw_char(' ', console_x, console_y, FB_DEFAULT_FG_COLOR, FB_DEFAULT_BG_COLOR);
    }
  } else {
    fb_draw_char(c, console_x, console_y, FB_DEFAULT_FG_COLOR, FB_DEFAULT_BG_COLOR);
    console_x += FONT_WIDTH;
  }

  // Handle line wrapping
  if (console_x >= fb_info->width) {
    console_x = 0;
    console_y += FONT_HEIGHT;
  }

  // Handle scrolling
  if (console_y >= fb_info->height) {
    fb_copy_region(FONT_HEIGHT, 0, fb_info->height - FONT_HEIGHT);
    // Clear the last line that was scrolled up into
    fb_draw_rect(0, fb_info->height - FONT_HEIGHT, fb_info->width, FONT_HEIGHT, FB_DEFAULT_BG_COLOR);
    console_y = fb_info->height - FONT_HEIGHT; // Keep cursor on the last line
  }

  fb_flush(); // Flush after every character to ensure immediate display
}

void klog_print_str(const char *s) {
  while (*s) {
    klog_putchar(*s++);
  }
}

void console_clear(void) {
  fb_clear(FB_DEFAULT_BG_COLOR);
  console_x = 0;
  console_y = 0;
}

void panic(const char *message, struct registers *regs) {
  // First, log the panic message to the serial port for debugging.
  serial_print("\n--- KERNEL PANIC ---\n");
  serial_print("Message: ");
  serial_print(message);
  serial_print("\n");

  // Now, display the full graphical panic screen.
  panic_screen_show(message, regs);

  // The panic_screen_show function should not return, but as a fallback,
  // ensure the system halts completely.
  for (;;) {
    __asm__ __volatile__("cli; hlt");
  }
}

// --- Stub/Init Functions ---

void log_init() {
  serial_init();
  // No need to clear framebuffer here, kernel.c will do fb_init and
  // console_clear
}

// --- klog implementation using vsprintf ---
void klog(log_level_t level, const char *fmt, ...) {
  // If the log level is panic, it should always be displayed.
  // Otherwise, filter based on configured log levels for each output.

  char log_buffer[LOG_MESSAGE_MAX_LEN * 2]; // Increased buffer for safety
  va_list args;

  va_start(args, fmt);
  int len = vksprintf(log_buffer, fmt, args);
  va_end(args);

  // Ensure null termination in case vksprintf overfills (though it shouldn't
  // with correct size)
  if ((size_t)len >= sizeof(log_buffer)) {
    log_buffer[sizeof(log_buffer) - 1] = '\0';
  } else {
    log_buffer[len] = '\0';
  }
  // Store in circular buffer
  strncpy(log_history[log_history_idx], log_buffer, LOG_MESSAGE_MAX_LEN - 1);
  log_history[log_history_idx][LOG_MESSAGE_MAX_LEN - 1] =
      '\0'; // Ensure null termination
  log_history_idx++;
  if (log_history_idx >= LOG_HISTORY_SIZE) {
    log_history_idx = 0;
    log_history_full = true;
  }

  // Output to serial (if level is appropriate, or it's a panic)
  if (level >= serial_log_level || level == LOG_PANIC) {
    serial_print("[");
    switch (level) {
    case LOG_INFO:
      serial_print("INFO");
      break;
    case LOG_WARN:
      serial_print("WARN");
      break;
    case LOG_ERROR:
      serial_print("ERROR");
      break;
    case LOG_DEBUG:
      serial_print("DEBUG");
      break;
    default:
      serial_print("UNKNOWN");
      break;
    }
    serial_print("] ");
    serial_print(log_buffer);
    serial_print("\n");
  }

  // Output to framebuffer (if level is appropriate, or it's a panic)
  if (level >= console_log_level || level == LOG_PANIC) {
    klog_print_str("[");
    switch (level) {
    case LOG_INFO:
      klog_print_str("INFO");
      break;
    case LOG_WARN:
      klog_print_str("WARN");
      break;
    case LOG_ERROR:
      klog_print_str("ERROR");
      break;
    case LOG_DEBUG:
      klog_print_str("DEBUG");
      break;
    default:
      klog_print_str("UNKNOWN");
      break;
    }
    klog_print_str("] ");
    klog_print_str(log_buffer);
    klog_print_str("\n");
  }
}

void log_get_entries(char *entries, int *count, int num_to_get) {
  if (!entries || !count || num_to_get <= 0) {
    if (count)
      *count = 0;
    return;
  }

  int num_available = log_history_full ? LOG_HISTORY_SIZE : log_history_idx;
  int num_to_copy = (num_to_get > num_available) ? num_available : num_to_get;
  *count = num_to_copy;

  int start_index =
      (log_history_idx - num_to_copy + LOG_HISTORY_SIZE) % LOG_HISTORY_SIZE;

  for (int i = 0; i < num_to_copy; i++) {
    int history_i = (start_index + i) % LOG_HISTORY_SIZE;

    char *dest = entries + (i * LOG_MESSAGE_MAX_LEN);
    char *src = log_history[history_i];
    strncpy(dest, src, LOG_MESSAGE_MAX_LEN - 1);
    dest[LOG_MESSAGE_MAX_LEN - 1] = '\0'; // Ensure null termination
  }
}