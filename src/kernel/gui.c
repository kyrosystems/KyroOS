#include "gui.h"
#include "fb.h"
#include "mouse.h"
#include "log.h"
#include "font.h" // For FONT_WIDTH

// Simple window structure
typedef struct {
    int x, y, width, height;
    const char* title;
} window_t;

#define MAX_WINDOWS 10
static window_t windows[MAX_WINDOWS];
static int num_windows = 0;

// Cursor bitmap (11x11)
static const uint8_t cursor_bitmap[11][11] = {
    {1,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,1,1,1,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
};
#define CURSOR_COLOR_1 0xFF000000 // Black
#define CURSOR_COLOR_2 0xFFFFFFFF // White

void gui_init() {
    num_windows = 0;
    klog(LOG_INFO, "GUI Initialized.");
}

void gui_create_window(const char* title, int x, int y, int width, int height) {
    if (num_windows < MAX_WINDOWS) {
        windows[num_windows].title = title;
        windows[num_windows].x = x;
        windows[num_windows].y = y;
        windows[num_windows].width = width;
        windows[num_windows].height = height;
        num_windows++;
    }
}

void gui_draw_rect(int x, int y, int width, int height, uint32_t color) {
    for (int i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) {
            fb_set_pixel(x + i, y + j, color);
        }
    }
}

void gui_draw_window(const char* title, int x, int y, int width, int height) {
    // Window background
    gui_draw_rect(x, y, width, height, 0xFFC0C0C0); // Light grey
    
    // Title bar
    gui_draw_rect(x, y, width, 20, 0xFF000080); // Dark blue
    
    // Border
    gui_draw_rect(x, y, width, 1, 0xFFFFFFFF); // Top
    gui_draw_rect(x, y + height - 1, width, 1, 0xFF808080); // Bottom
    gui_draw_rect(x, y, 1, height, 0xFFFFFFFF); // Left
    gui_draw_rect(x + width - 1, y, 1, height, 0xFF808080); // Right

    // Title text
    int i = 0;
    while(title[i] != '\0') {
        fb_draw_char(title[i], x + 5 + (i * FONT_WIDTH), y + 4, 0xFFFFFFFF, 0xFF000080);
        i++;
    }
}

void gui_draw_cursor() {
    int32_t mouse_x, mouse_y;
    mouse_get_position(&mouse_x, &mouse_y);

    for (int y = 0; y < 11; y++) {
        for (int x = 0; x < 11; x++) {
            if (cursor_bitmap[y][x] == 1) {
                fb_set_pixel(mouse_x + x, mouse_y + y, CURSOR_COLOR_1);
            } else if (cursor_bitmap[y][x] == 2) {
                fb_set_pixel(mouse_x + x, mouse_y + y, CURSOR_COLOR_2);
            }
        }
    }
}

void gui_update() {
    // This is a very inefficient redraw-all loop
    fb_clear(0xFF303080); // Desktop background color
    
    for (int i = 0; i < num_windows; i++) {
        gui_draw_window(windows[i].title, windows[i].x, windows[i].y, windows[i].width, windows[i].height);
    }
    
    gui_draw_cursor();
}