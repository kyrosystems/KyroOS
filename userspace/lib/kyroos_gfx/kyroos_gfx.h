#ifndef KYROOS_GFX_H
#define KYROOS_GFX_H

#include <stdint.h>
#include <stdbool.h>

// Event definitions mirrored from kernel
typedef enum {
    GFX_EVENT_NONE = 0,
    GFX_EVENT_KEY_DOWN = 1,
    GFX_EVENT_KEY_UP = 2,
    GFX_EVENT_MOUSE_MOVE = 3,
    GFX_EVENT_MOUSE_DOWN = 4,
    GFX_EVENT_MOUSE_UP = 5
} gfx_event_type_t;

typedef struct {
    uint32_t type;
    int32_t data1; // Character / Keycode / Mouse X
    int32_t data2; // Scancode / Mouse Y
    int32_t data3; // Buttons / Extra
} gfx_event_t;

#ifndef _USER_FB_INFO_T_
#define _USER_FB_INFO_T_
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    void *buffer;
} user_fb_info_t;
#endif

int gfx_init(void);
int gfx_compositor_init(void);
void gfx_clear(uint32_t color);
void gfx_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void gfx_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
void gfx_draw_hud(const char *api_name, uint32_t fps, uint32_t frame_time_ms, uint64_t mem_used_kb, uint64_t mem_total_kb);
void gfx_draw_cursor(int x, int y);
void gfx_set_clip(int x0, int y0, int x1, int y1);
void gfx_reset_clip(void);
bool gfx_is_clipped(int x, int y);
void gfx_flip(void);
int gfx_poll_event(gfx_event_t* event);
uint32_t gfx_get_width(void);
uint32_t gfx_get_height(void);
uint32_t gfx_get_pitch(void);
uint32_t *gfx_get_buffer(void);

#endif // KYROOS_GFX_H
