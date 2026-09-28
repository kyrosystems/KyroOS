#ifndef GUI_H
#define GUI_H

#include <stdint.h>

void gui_init();
void gui_draw_rect(int x, int y, int width, int height, uint32_t color);
void gui_draw_window(const char* title, int x, int y, int width, int height);
void gui_draw_cursor();
void gui_update(); // Redraws the entire screen

#endif // GUI_H
