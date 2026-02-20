#ifndef FONT_H
#define FONT_H

#include <stdint.h>

// A simple 8x16 bitmap font (usually stored in a separate file, but embedded for simplicity)
// This is a placeholder for a real font.
// The real font data would be a large array.
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
extern const uint8_t default_font[256][FONT_HEIGHT];

#endif // FONT_H
