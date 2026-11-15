#include "GL/gl.h"
#include <kyroos_gfx.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float x, y, z, w;
    float r, g, b, a;
    float u, v;
    float nx, ny, nz;
    float sx, sy, sz;
} gl_vertex_t;

extern uint32_t *gfx_get_buffer(void);
extern uint32_t gfx_get_width(void);
extern uint32_t gfx_get_height(void);
extern GLboolean glIsEnabled(GLenum cap);

// Helper min/max
static inline int min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}
static inline int max3(int a, int b, int c) {
    int m = a > b ? a : b;
    return m > c ? m : c;
}
static inline float edge_func(float x0, float y0, float x1, float y1, float x, float y) {
    return (x - x0) * (y1 - y0) - (y - y0) * (x1 - x0);
}

void gl_rasterize_triangle(const gl_vertex_t *v0, const gl_vertex_t *v1, const gl_vertex_t *v2) {
    uint32_t width = gfx_get_width();
    uint32_t height = gfx_get_height();
    uint32_t *color_buf = gfx_get_buffer();
    uint32_t *depth_buf = (uint32_t *)((uint64_t)color_buf + (width * height * 4));

    bool depth_test = glIsEnabled(GL_DEPTH_TEST);

    // Screen coordinates
    float x0 = v0->sx, y0 = v0->sy;
    float x1 = v1->sx, y1 = v1->sy;
    float x2 = v2->sx, y2 = v2->sy;

    float area = edge_func(x0, y0, x1, y1, x2, y2);
    if (area == 0.0f) return;

    // Backface culling
    if (glIsEnabled(GL_CULL_FACE) && area < 0.0f) return;

    float inv_area = 1.0f / area;

    // Bounding box
    int min_x = min3((int)x0, (int)x1, (int)x2);
    int max_x = max3((int)x0, (int)x1, (int)x2);
    int min_y = min3((int)y0, (int)y1, (int)y2);
    int max_y = max3((int)y0, (int)y1, (int)y2);

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int)width)  max_x = (int)width - 1;
    if (max_y >= (int)height) max_y = (int)height - 1;

    for (int y = min_y; y <= max_y; y++) {
        uint32_t row = y * width;
        for (int x = min_x; x <= max_x; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;

            float w0 = edge_func(x1, y1, x2, y2, px, py);
            float w1 = edge_func(x2, y2, x0, y0, px, py);
            float w2 = edge_func(x0, y0, x1, y1, px, py);

            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                float b0 = w0 * inv_area;
                float b1 = w1 * inv_area;
                float b2 = w2 * inv_area;

                float z = b0 * v0->sz + b1 * v1->sz + b2 * v2->sz;
                if (z < 0.0f || z > 1.0f) continue;

                uint32_t iz = (uint32_t)(z * 4294967295.0f);
                uint32_t idx = row + x;

                if (!depth_test || iz < depth_buf[idx]) {
                    if (depth_test) depth_buf[idx] = iz;

                    // Interpolate Gouraud color
                    float r = b0 * v0->r + b1 * v1->r + b2 * v2->r;
                    float g = b0 * v0->g + b1 * v1->g + b2 * v2->g;
                    float b = b0 * v0->b + b1 * v1->b + b2 * v2->b;

                    uint32_t ir = (uint32_t)(r * 255.0f) & 0xFF;
                    uint32_t ig = (uint32_t)(g * 255.0f) & 0xFF;
                    uint32_t ib = (uint32_t)(b * 255.0f) & 0xFF;

                    color_buf[idx] = 0xFF000000 | (ir << 16) | (ig << 8) | ib;
                }
            }
        }
    }
}

static inline int abs_i(int x) { return x < 0 ? -x : x; }

void gl_rasterize_line(const gl_vertex_t *v0, const gl_vertex_t *v1) {
    uint32_t width = gfx_get_width();
    uint32_t height = gfx_get_height();
    uint32_t *color_buf = gfx_get_buffer();

    int x0 = (int)v0->sx, y0 = (int)v0->sy;
    int x1 = (int)v1->sx, y1 = (int)v1->sy;

    int dx = abs_i(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs_i(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    uint32_t ir = (uint32_t)(v0->r * 255.0f) & 0xFF;
    uint32_t ig = (uint32_t)(v0->g * 255.0f) & 0xFF;
    uint32_t ib = (uint32_t)(v0->b * 255.0f) & 0xFF;
    uint32_t c = 0xFF000000 | (ir << 16) | (ig << 8) | ib;

    while (1) {
        if (x0 >= 0 && (uint32_t)x0 < width && y0 >= 0 && (uint32_t)y0 < height) {
            color_buf[y0 * width + x0] = c;
        }
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
