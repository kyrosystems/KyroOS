#include "fb.h"
#include "heap.h"
#include "kstring.h"

static fb_info_t current_fb;
static uint32_t *back_buffer = NULL;
static bool back_buffer_ready = false;

static uint32_t dirty_top = 0, dirty_bottom = 0; 
static bool     dirty_valid = false;

static inline void fb_mark_dirty(uint32_t y0, uint32_t y1) {
  if (!back_buffer_ready) return;
  if (y0 > y1) { uint32_t t = y0; y0 = y1; y1 = t; }
  if (y0 >= current_fb.height) return;
  if (y1 > current_fb.height) y1 = current_fb.height;
  if (!dirty_valid) {
    dirty_top = y0; dirty_bottom = y1; dirty_valid = true;
  } else {
    if (y0 < dirty_top) dirty_top = y0;
    if (y1 > dirty_bottom) dirty_bottom = y1;
  }
}

void fb_init(struct limine_framebuffer *fb) {
  current_fb.address = (uint64_t)fb->address;
  current_fb.width = fb->width;
  current_fb.height = fb->height;
  current_fb.pitch = fb->pitch;
  current_fb.bpp = fb->bpp;
}

void fb_init_backbuffer() {
  uint64_t size = current_fb.height * current_fb.pitch;
  back_buffer = (uint32_t *)kmalloc(size);
  if (back_buffer) {
    memset(back_buffer, 0, size);
    back_buffer_ready = true;
  }
}

bool fb_is_backbuffer_initialized() { return back_buffer_ready; }

void fb_set_pixel(uint32_t x, uint32_t y, uint32_t color) {
  if (!back_buffer_ready || x >= current_fb.width || y >= current_fb.height) return;
  back_buffer[y * (current_fb.pitch / 4) + x] = color;
  if (!dirty_valid) { dirty_top = y; dirty_bottom = y + 1; dirty_valid = true; }
  else {
    if (y < dirty_top) dirty_top = y;
    if (y + 1 > dirty_bottom) dirty_bottom = y + 1;
  }
}

uint32_t fb_get_pixel(uint32_t x, uint32_t y) {
  if (!back_buffer_ready || x >= current_fb.width || y >= current_fb.height) return 0;
  return back_buffer[y * (current_fb.pitch / 4) + x];
}

void fb_clear(uint32_t color) {
  if (!back_buffer_ready) return;
  // Faster clear for 32bpp using memset if color is 0, else loop
  if (color == 0) {
      memset(back_buffer, 0, current_fb.height * current_fb.pitch);
  } else {
      for (uint32_t i = 0; i < current_fb.width * current_fb.height; i++) back_buffer[i] = color;
  }
  fb_mark_all_dirty();
}

void fb_draw_bitmap(int x, int y, const uint32_t *data, int w, int h) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint32_t pixel = data[row * w + col];
            uint8_t a = (pixel >> 24) & 0xFF;
            if (a > 128)  
                fb_set_pixel(x + col, y + row, pixel & 0x00FFFFFF);
        }
    }
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
  for (uint32_t i = 0; i < h; i++) {
    for (uint32_t j = 0; j < w; j++) {
      fb_set_pixel(x + j, y + i, color);
    }
  }
}

extern uint8_t font_bitmap[256][16];
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

void fb_draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
  uint8_t *glyph = font_bitmap[(uint8_t)c];
  for (int i = 0; i < FONT_HEIGHT; i++) {
    for (int j = 0; j < FONT_WIDTH; j++) {
      if (glyph[i] & (1 << (7 - j))) fb_set_pixel(x + j, y + i, fg);
      else if (bg != 0) fb_set_pixel(x + j, y + i, bg);
    }
  }
}

void fb_flush() {
  if (!back_buffer_ready) return;
  memcpy((void *)current_fb.address, back_buffer, current_fb.height * current_fb.pitch);
  dirty_valid = false;
}

void fb_mark_all_dirty(void) {
  if (!back_buffer_ready) return;
  dirty_top = 0;
  dirty_bottom = current_fb.height;
  dirty_valid = true;
}

bool fb_flush_dirty(void) {
  if (!back_buffer_ready || !dirty_valid) return false;
  if (dirty_bottom <= dirty_top) { dirty_valid = false; return false; }
  uint64_t off_px  = (uint64_t)dirty_top * (current_fb.pitch / 4);
  uint64_t off_byt = (uint64_t)dirty_top * current_fb.pitch;
  uint64_t rows    = (uint64_t)(dirty_bottom - dirty_top);
  memcpy((uint8_t *)current_fb.address + off_byt,
         back_buffer + off_px, (size_t)(rows * current_fb.pitch));
  dirty_valid = false;
  return true;
}

void fb_copy_region(uint32_t src_y, uint32_t dst_y, uint32_t h) {
  if (!back_buffer_ready) return;
  uint32_t row_size = current_fb.pitch;
  if (src_y + h > current_fb.height || dst_y + h > current_fb.height) return;
  memcpy(back_buffer + (dst_y * row_size / 4), back_buffer + (src_y * row_size / 4), h * row_size);
  fb_mark_all_dirty();
}

const fb_info_t *fb_get_info() { return &current_fb; }
