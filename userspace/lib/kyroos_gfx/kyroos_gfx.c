#include "kyroos_gfx.h"
#include <display.h>
#include <sysnum.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SYS_GFX_GET_FB_INFO 25
#define SYS_INPUT_POLL_EVENT 26
#define SYS_SBRK 27

static user_fb_info_t fb_info;
static uint32_t *backbuffer = NULL;
static size_t backbuffer_size = 0;
static bool window_surface = false;

static long raw_syscall(long number, long arg1, long arg2, long arg3) {
    register long r_num __asm__("rax") = number;
    register long r_a1  __asm__("rdi") = arg1;
    register long r_a2  __asm__("rsi") = arg2;
    register long r_a3  __asm__("rdx") = arg3;
    __asm__ __volatile__("int $0x80"
                         : "+a"(r_num)
                         : "r"(r_a1), "r"(r_a2), "r"(r_a3)
                         : "memory", "rcx", "r11");
    return r_num;
}

int gfx_compositor_init(void) {
    if (raw_syscall(SYS_DISPLAY_CTL, DISPLAY_REGISTER, 0, 0) != 0) return -1;
    return gfx_init();
}

int gfx_init(void) {
    raw_syscall(SYS_DISPLAY_CTL, DISPLAY_REGISTER, 0, 0);
    display_surface_info_t surface;
    if (raw_syscall(SYS_DISPLAY_CTL, DISPLAY_CREATE, (long)&surface, 0) == 0) {
        size_t required = (size_t)surface.pitch * surface.height;
        if (!backbuffer || backbuffer_size < required) {
            long memory = raw_syscall(SYS_SBRK, required, 0, 0);
            if (memory <= 0) return -1;
            backbuffer = (uint32_t *)memory;
            backbuffer_size = required;
        }
        fb_info = (user_fb_info_t){surface.width, surface.height, surface.pitch, 32, backbuffer};
        window_surface = true;
        return 0;
    }
    if (raw_syscall(SYS_GFX_GET_FB_INFO, (long)&fb_info, 0, 0) == 0) {
        if (fb_info.buffer != NULL && fb_info.width > 0 && fb_info.height > 0) {
            size_t required = (size_t)fb_info.pitch * fb_info.height;
            if (fb_info.bpp != 32 || fb_info.pitch < fb_info.width * 4) return -1;
            if (backbuffer && backbuffer_size >= required) return 0;
            backbuffer_size = required;
            // Allocate backbuffer using sbrk or direct buffer pointer
            void *cur_brk = (void *)raw_syscall(SYS_SBRK, 0, 0, 0);
            if ((int64_t)cur_brk > 0) {
                void *new_brk = (void *)raw_syscall(SYS_SBRK, backbuffer_size + 4096, 0, 0);
                if ((int64_t)new_brk > 0) {
                    backbuffer = (uint32_t *)cur_brk;
                }
            }
            if (!backbuffer) {
                // Fallback to rendering directly into framebuffer
                backbuffer = (uint32_t *)fb_info.buffer;
            }
            return 0; // Success
        }
    }
    return -1;
}

uint32_t *gfx_get_buffer(void) {
    return backbuffer ? backbuffer : (uint32_t *)fb_info.buffer;
}

uint32_t gfx_get_width(void) {
    return fb_info.width;
}

uint32_t gfx_get_height(void) {
    return fb_info.height;
}

uint32_t gfx_get_pitch(void) {
    return fb_info.pitch;
}

void gfx_clear(uint32_t color) {
    if (!fb_info.buffer) return;
    uint32_t *buf = gfx_get_buffer();
    size_t pixels = (fb_info.pitch / 4) * fb_info.height;
    for (size_t i = 0; i < pixels; i++) {
        buf[i] = color;
    }
}

static bool s_clip_active = false;
static int32_t s_clip_x0 = 0, s_clip_y0 = 0, s_clip_x1 = 0, s_clip_y1 = 0;

void gfx_set_clip(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    s_clip_active = true;
    s_clip_x0 = x0;
    s_clip_y0 = y0;
    s_clip_x1 = x1;
    s_clip_y1 = y1;
}

void gfx_reset_clip(void) {
    s_clip_active = false;
}

bool gfx_is_clipped(int x, int y) {
    if (!s_clip_active) return false;
    if (x < s_clip_x0 || x >= s_clip_x1 || y < s_clip_y0 || y >= s_clip_y1) return true;
    return false;
}

void gfx_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_info.width || y >= fb_info.height) return;
    if (s_clip_active) {
        if ((int32_t)x < s_clip_x0 || (int32_t)x >= s_clip_x1 ||
            (int32_t)y < s_clip_y0 || (int32_t)y >= s_clip_y1) return;
    }
    uint32_t *buf = gfx_get_buffer();
    uint32_t stride = fb_info.pitch / 4;
    buf[y * stride + x] = color;
}

void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (x >= fb_info.width || y >= fb_info.height) return;
    int32_t rx0 = (int32_t)x;
    int32_t ry0 = (int32_t)y;
    int32_t rx1 = (int32_t)(x + width);
    int32_t ry1 = (int32_t)(y + height);
    if (rx1 > (int32_t)fb_info.width) rx1 = (int32_t)fb_info.width;
    if (ry1 > (int32_t)fb_info.height) ry1 = (int32_t)fb_info.height;

    if (s_clip_active) {
        if (rx0 < s_clip_x0) rx0 = s_clip_x0;
        if (ry0 < s_clip_y0) ry0 = s_clip_y0;
        if (rx1 > s_clip_x1) rx1 = s_clip_x1;
        if (ry1 > s_clip_y1) ry1 = s_clip_y1;
    }
    if (rx0 >= rx1 || ry0 >= ry1) return;

    uint32_t *buf = gfx_get_buffer();
    uint32_t stride = fb_info.pitch / 4;

    for (int32_t j = ry0; j < ry1; j++) {
        uint32_t row = (uint32_t)j * stride + (uint32_t)rx0;
        for (int32_t i = 0; i < rx1 - rx0; i++) {
            buf[row + i] = color;
        }
    }
}

static inline int abs_val(int x) { return x < 0 ? -x : x; }

void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs_val(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs_val(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        if (x0 >= 0 && (uint32_t)x0 < fb_info.width && y0 >= 0 && (uint32_t)y0 < fb_info.height) {
            gfx_draw_pixel((uint32_t)x0, (uint32_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_flip(void) {
    if (window_surface) {
        raw_syscall(SYS_DISPLAY_CTL, DISPLAY_PRESENT, (long)backbuffer, 0);
        return;
    }
    if (!fb_info.buffer || !backbuffer || backbuffer == (uint32_t *)fb_info.buffer) return;
    uint64_t *dst = (uint64_t *)fb_info.buffer;
    uint64_t *src = (uint64_t *)backbuffer;
    size_t words = ((size_t)fb_info.pitch * fb_info.height) / sizeof(uint64_t);
    for (size_t i = 0; i < words; i++) {
        dst[i] = src[i];
    }
}

int gfx_poll_event(gfx_event_t *event) {
    if (!event) return 0;
    return (int)raw_syscall(SYS_INPUT_POLL_EVENT, (long)event, 0, 0);
}

extern const unsigned font_aa_count;
extern const uint32_t font_aa_points[];
extern const uint8_t font_aa[][128];

static void draw_codepoint(uint32_t x, uint32_t y, uint32_t code, uint32_t fg, uint32_t bg) {
    unsigned lo = 0, hi = font_aa_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (font_aa_points[mid] < code) lo = mid + 1;
        else hi = mid;
    }
    if (lo == font_aa_count || font_aa_points[lo] != code) {
        if (code == '?') return;
        draw_codepoint(x, y, '?', fg, bg); return;
    }
    uint32_t *pixels = gfx_get_buffer();
    uint32_t stride = fb_info.pitch / 4;
    for (unsigned row = 0; row < 16; row++) for (unsigned col = 0; col < 8; col++) {
        uint32_t px = x + col, py = y + row;
        if (px >= fb_info.width || py >= fb_info.height || gfx_is_clipped(px, py)) continue;
        unsigned alpha = font_aa[lo][row * 8 + col];
        uint32_t base = bg ? bg : pixels[py * stride + px];
        unsigned red = (((fg >> 16) & 255) * alpha + ((base >> 16) & 255) * (255 - alpha)) / 255;
        unsigned green = (((fg >> 8) & 255) * alpha + ((base >> 8) & 255) * (255 - alpha)) / 255;
        unsigned blue = ((fg & 255) * alpha + (base & 255) * (255 - alpha)) / 255;
        pixels[py * stride + px] = 0xff000000 | (red << 16) | (green << 8) | blue;
    }
}
void gfx_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    draw_codepoint(x, y, (unsigned char)c, fg, bg);
}
void gfx_draw_string(uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg) {
    if (!text) return;
    const unsigned char *s = (const unsigned char *)text;
    uint32_t cur_x = x;
    while (*s) {
        uint32_t code = *s++;
        if (code >= 0xc2 && code <= 0xdf && (s[0] & 0xc0) == 0x80) {
            code = ((code & 31) << 6) | (*s++ & 63);
        } else if (code >= 0xe0 && code <= 0xef && s[0] && s[1] &&
                   (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) {
            code = ((code & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2;
        } else if (code >= 128) code = '?';
        if (code == '\n') { y += 18; cur_x = x; continue; }
        if (code == '\t') { cur_x += 32; continue; }
        if (code < 32) continue;
        draw_codepoint(cur_x, y, code, fg, bg);
        cur_x += 8;
    }
}

static void u32_to_str(uint32_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[16];
    int i = 0;
    while (val > 0) { tmp[i++] = (val % 10) + '0'; val /= 10; }
    int j = 0;
    while (i > 0) { buf[j++] = tmp[--i]; }
    buf[j] = '\0';
}

void gfx_draw_hud(const char *api_name, uint32_t fps, uint32_t frame_time_ms, uint64_t mem_used_kb, uint64_t mem_total_kb) {
    uint32_t sw = fb_info.width;
    if (sw < 320) return;

    uint32_t box_w = 260;
    uint32_t box_h = 96;
    uint32_t box_x = sw - box_w - 12;
    uint32_t box_y = 12;

    gfx_draw_rect(box_x, box_y, box_w, box_h, 0xDD10141D);
    gfx_draw_rect(box_x, box_y, box_w, 2, 0xFF00D2FF);
    gfx_draw_rect(box_x, box_y + box_h - 1, box_w, 1, 0xFF005577);
    gfx_draw_rect(box_x, box_y, 1, box_h, 0xFF005577);
    gfx_draw_rect(box_x + box_w - 1, box_y, 1, box_h, 0xFF005577);

    gfx_draw_string(box_x + 10, box_y + 8, api_name ? api_name : "KyroOS Graphics", 0xFF00E5FF, 0);

    char fps_buf[16], ft_buf[16];
    u32_to_str(fps, fps_buf);
    u32_to_str(frame_time_ms, ft_buf);
    char line1[64] = "FPS: ";
    int p = 5;
    for (int k = 0; fps_buf[k]; k++) line1[p++] = fps_buf[k];
    const char *sep = " | Frame: ";
    for (int k = 0; sep[k]; k++) line1[p++] = sep[k];
    for (int k = 0; ft_buf[k]; k++) line1[p++] = ft_buf[k];
    const char *ms = " ms";
    for (int k = 0; ms[k]; k++) line1[p++] = ms[k];
    line1[p] = '\0';
    gfx_draw_string(box_x + 10, box_y + 30, line1, 0xFF55FF88, 0);

    char rw[16], rh[16];
    u32_to_str(fb_info.width, rw);
    u32_to_str(fb_info.height, rh);
    char line2[64] = "Resolution: ";
    p = 12;
    for (int k = 0; rw[k]; k++) line2[p++] = rw[k];
    line2[p++] = 'x';
    for (int k = 0; rh[k]; k++) line2[p++] = rh[k];
    line2[p] = '\0';
    gfx_draw_string(box_x + 10, box_y + 50, line2, 0xFFAAAAAA, 0);

    uint32_t used_mb = (uint32_t)(mem_used_kb / 1024);
    uint32_t tot_mb = (uint32_t)(mem_total_kb / 1024);
    char mu[16], mt[16];
    u32_to_str(used_mb, mu);
    u32_to_str(tot_mb, mt);
    char line3[64] = "Memory: ";
    p = 8;
    for (int k = 0; mu[k]; k++) line3[p++] = mu[k];
    const char *msep = "MB / ";
    for (int k = 0; msep[k]; k++) line3[p++] = msep[k];
    for (int k = 0; mt[k]; k++) line3[p++] = mt[k];
    line3[p++] = 'M'; line3[p++] = 'B';
    line3[p] = '\0';
    gfx_draw_string(box_x + 10, box_y + 70, line3, 0xFFFFDD33, 0);
}

static const uint16_t cursor_mask[16] = {
    0b1000000000000000,
    0b1100000000000000,
    0b1110000000000000,
    0b1111000000000000,
    0b1111100000000000,
    0b1111110000000000,
    0b1111111000000000,
    0b1111111100000000,
    0b1111111110000000,
    0b1111110000000000,
    0b1101111000000000,
    0b1000111100000000,
    0b0000011110000000,
    0b0000001111000000,
    0b0000000111000000,
    0b0000000000000000
};

static const uint16_t cursor_border[16] = {
    0b1100000000000000,
    0b1010000000000000,
    0b1001000000000000,
    0b1000100000000000,
    0b1000010000000000,
    0b1000001000000000,
    0b1000000100000000,
    0b1000000010000000,
    0b1000000001000000,
    0b1000011111000000,
    0b1010010000000000,
    0b1101001000000000,
    0b0000100100000000,
    0b0000010010000000,
    0b0000001110000000,
    0b0000000000000000
};

void gfx_draw_cursor(int x, int y) {
    for (int r = 0; r < 16; r++) {
        int py = y + r;
        if (py < 0 || (uint32_t)py >= fb_info.height) continue;
        for (int c = 0; c < 16; c++) {
            int px = x + c;
            if (px < 0 || (uint32_t)px >= fb_info.width) continue;

            // Draw subtle drop shadow first for extreme contrast
            if ((cursor_border[r] >> (15 - c)) & 1) {
                if (px + 1 < (int)fb_info.width && py + 1 < (int)fb_info.height) {
                    gfx_draw_pixel(px + 1, py + 1, 0xFF0A0E14);
                }
            }
            // Inner white fill
            if ((cursor_mask[r] >> (15 - c)) & 1) {
                gfx_draw_pixel(px, py, 0xFFFFFFFF);
            }
            // Crisp solid black outline
            if ((cursor_border[r] >> (15 - c)) & 1) {
                gfx_draw_pixel(px, py, 0xFF000000);
            }
        }
    }
}
