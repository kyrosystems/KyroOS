#ifndef TUI_H
#define TUI_H

#include <stdint.h>

// A simple struct for framebuffer info
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    void* buffer;
} fb_info_t;

int tui_init(fb_info_t* info);
void tui_draw_box(const fb_info_t* info, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void tui_draw_text(const fb_info_t* info, uint32_t x, uint32_t y, const char* text, uint32_t color);
void tui_clear_screen(const fb_info_t* info, uint32_t color);

#endif // TUI_H
