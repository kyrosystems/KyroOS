#include "kyroos_gfx.h"
#include <stddef.h> // for NULL

// Syscall numbers mirrored from kernel/syscall.h
#define SYS_GFX_GET_FB_INFO 6
#define SYS_INPUT_POLL_EVENT 7

// Framebuffer info struct mirrored from kernel/fb.h
typedef struct {
    uint32_t* address;
    uint32_t width;
    uint32_t height;
    uint16_t pitch;
    uint8_t bpp;
} fb_info_t;

static fb_info_t fb_info;

// Raw syscall function
static long syscall(long number, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile (
        "mov %1, %%rax\n\t"
        "mov %2, %%rdi\n\t"
        "mov %3, %%rsi\n\t"
        "mov %4, %%rdx\n\t"
        "int $0x80\n\t"
        "mov %%rax, %0"
        : "=g"(ret)
        : "g"(number), "g"(arg1), "g"(arg2), "g"(arg3)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11"
    );
    return ret;
}

int gfx_init() {
    if (syscall(SYS_GFX_GET_FB_INFO, (long)&fb_info, 0, 0) == 0) {
        if (fb_info.address != NULL && fb_info.bpp == 32) {
            return 0; // Success
        }
    }
    return -1; // Failure
}

void gfx_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_info.width || y >= fb_info.height) {
        return;
    }
    uint64_t offset = (y * fb_info.pitch) + (x * (fb_info.bpp / 8));
    *(uint32_t*)((uint64_t)fb_info.address + offset) = color;
}

void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    for (uint32_t j = 0; j < height; j++) {
        for (uint32_t i = 0; i < width; i++) {
            gfx_draw_pixel(x + i, y + j, color);
        }
    }
}

int gfx_poll_event(gfx_event_t* event) {
    return syscall(SYS_INPUT_POLL_EVENT, (long)event, 0, 0);
}

uint32_t gfx_get_width() {
    return fb_info.width;
}

uint32_t gfx_get_height() {
    return fb_info.height;
}
