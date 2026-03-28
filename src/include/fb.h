#ifndef FB_H
#define FB_H

#include <stdint.h>
#include <stdbool.h>
#include "limine.h"

typedef struct {
    uint64_t address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint16_t bpp;
} fb_info_t;

void fb_init(struct limine_framebuffer *fb);
void fb_init_backbuffer();
bool fb_is_backbuffer_initialized();
void fb_clear(uint32_t color);
void fb_set_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t fb_get_pixel(uint32_t x, uint32_t y);
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);
void fb_flush();
void fb_copy_region(uint32_t src_y, uint32_t dst_y, uint32_t h);
const fb_info_t *fb_get_info();

#endif
