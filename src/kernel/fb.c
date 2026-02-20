#include "fb.h"
#include "font.h"
#include "heap.h"
#include "kstring.h"
#include "limine.h"
#include "log.h"
#include "vmm.h"
#include <stdbool.h>

extern uint64_t kernel_hhdm_offset;
extern pml4_t* kernel_pml4;

static struct limine_framebuffer *fb_tag_global_ptr = NULL;
static uint32_t *backbuffer = NULL;
static size_t backbuffer_size = 0;

void fb_init(struct limine_framebuffer *fb_tag) {
    fb_tag_global_ptr = fb_tag;

    if (fb_tag_global_ptr) {
        if (fb_tag_global_ptr->bpp < 24) {
            klog(LOG_ERROR, "Framebuffer: BPP is less than 24. Skipping initialization.");
            return;
        }
        klog(LOG_INFO, "Framebuffer: Original address: %p", fb_tag_global_ptr->address);
        klog(LOG_INFO, "Framebuffer: HHDM offset: %p", (void*)kernel_hhdm_offset);
        // The framebuffer's address provided by Limine is already a virtual address,
        // so no need to add hhdm_offset.
        // fb_tag_global_ptr->address = (void*)((uint64_t)fb_tag_global_ptr->address + hhdm_offset);
        klog(LOG_INFO, "Framebuffer: Mapped address: %p", fb_tag_global_ptr->address);

        // No VMM mapping for now, just to get a clean compile and test file writes.
        // We will add this back once we confirm basic functionality.

    } else {
        klog(LOG_ERROR, "Framebuffer: fb_tag is NULL.");
        return;
    }
}

void fb_init_backbuffer(void) {
    if (!fb_tag_global_ptr)
        return;
    backbuffer_size = fb_tag_global_ptr->height * fb_tag_global_ptr->pitch;
    backbuffer = (uint32_t *)kmalloc(backbuffer_size);
    if (backbuffer) {
        memcpy(backbuffer, fb_tag_global_ptr->address, backbuffer_size);
        klog(LOG_INFO, "Framebuffer: Backbuffer enabled.");
    }
}

void fb_flush(void) {
    if (backbuffer && fb_tag_global_ptr && fb_tag_global_ptr->address) {
        memcpy(fb_tag_global_ptr->address, backbuffer, backbuffer_size);
    }
}

void fb_put_pixel(int x, int y, uint32_t color) {
    if (!backbuffer) return;

    const fb_info_t *fb_info = fb_get_info();
    if (x >= 0 && x < (int)fb_info->width && y >= 0 && y < (int)fb_info->height) {
        // pitch is in bytes, but backbuffer is uint32_t*, so we divide by 4
        uint64_t offset_pixels = (y * (fb_info->pitch / 4)) + x;
        backbuffer[offset_pixels] = color;
    }
}

void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    fb_put_pixel((int)x, (int)y, color);
}

void fb_clear(uint32_t color) {
    const fb_info_t *fb_info = fb_get_info();
    if (!fb_info) {
        return;
    }
    fb_draw_rect(0, 0, fb_info->width, fb_info->height, color);
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!backbuffer) return;

    const fb_info_t *fb_info = fb_get_info();

    for (int curr_y = y; curr_y < y + h; curr_y++) {
        if (curr_y < 0 || curr_y >= (int)fb_info->height)
            continue;
        
        // Pre-calculate line offset
        uint32_t* line = backbuffer + (curr_y * (fb_info->pitch / 4));
        int start_x = (x < 0) ? 0 : x;
        int end_x = (x + w > (int)fb_info->width) ? (int)fb_info->width : x + w;

        for (int curr_x = start_x; curr_x < end_x; curr_x++) {
            line[curr_x] = color;
        }
    }
}

void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg) {
    if (!backbuffer) return;

    const fb_info_t *fb_info = fb_get_info();
    const uint8_t *glyph = default_font[(uint8_t)c];

    for (uint32_t cy = 0; cy < 16; cy++) {
        if (y + (int)cy < 0 || y + (int)cy >= (int)fb_info->height) continue;
        uint8_t row = glyph[cy];
        uint32_t* line = backbuffer + ((y + cy) * (fb_info->pitch / 4));
        for (uint32_t cx = 0; cx < 8; cx++) {
            if (x + (int)cx < 0 || x + (int)cx >= (int)fb_info->width) continue;
            uint32_t color = ((row >> (7 - cx)) & 1) ? fg : bg;
            line[x + cx] = color;
        }
    }
}

void fb_copy_region(int src_y, int dest_y, int rows) {
    if (!backbuffer) return;

    const fb_info_t *fb_info = fb_get_info();
    uint64_t pitch_in_dwords = fb_info->pitch / 4;
    uint32_t *dst_addr = backbuffer + (dest_y * pitch_in_dwords);
    uint32_t *src_addr = backbuffer + (src_y * pitch_in_dwords);
    size_t size_in_dwords = rows * pitch_in_dwords;
    
    // Use memmove because regions can overlap during scrolling
    memmove(dst_addr, src_addr, size_in_dwords * 4);
}

const fb_info_t *fb_get_info(void) {
    if (!fb_tag_global_ptr) {
    }
    return fb_tag_global_ptr;
}
bool fb_is_backbuffer_initialized(void) {
    return backbuffer != NULL;
}

void fb_draw_image(const uint32_t* pixels, uint32_t width, uint32_t height, uint32_t x, uint32_t y) {
    const fb_info_t *fb_info = fb_get_info();
    if (!fb_info || !fb_info->address || !pixels)
        return;
    
    for (uint32_t img_y = 0; img_y < height; img_y++) {
        for (uint32_t img_x = 0; img_x < width; img_x++) {
            uint32_t color = pixels[img_y * width + img_x];
            int screen_x = x + img_x;
            int screen_y = y + img_y;
            
            if (screen_x >= 0 && screen_x < (int)fb_info->width &&
                screen_y >= 0 && screen_y < (int)fb_info->height) {
                fb_put_pixel(screen_x, screen_y, color);
            }
        }
    }
}