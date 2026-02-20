#ifndef FB_H
#define FB_H

#include <stdbool.h>
#include "limine.h"
#include <stdint.h>

typedef struct limine_framebuffer fb_info_t;

void fb_init(struct limine_framebuffer *fb_tag);
void fb_init_backbuffer(void);
void fb_flush(void);
void fb_put_pixel(int x, int y, uint32_t color);
void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_draw_image(const uint32_t* pixels, uint32_t width, uint32_t height, uint32_t x, uint32_t y);
void fb_clear(uint32_t color);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg);
void fb_copy_region(int src_y, int dest_y, int rows);
const fb_info_t *fb_get_info(void);
bool fb_is_backbuffer_initialized(void);

#endif // FB_H