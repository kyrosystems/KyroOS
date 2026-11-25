#include <kyroolib.h>
#include <kyroos_gfx.h>
#include <stdint.h>
#include <stdbool.h>

static int isin(int d) {
    d %= 360; if (d < 0) d += 360;
    static const int16_t s[91] = {
        0,17,34,52,69,87,104,121,139,156,173,190,207,224,241,258,275,292,309,325,
        342,358,374,390,406,422,438,453,469,484,499,515,529,544,559,573,587,601,
        615,629,642,656,669,682,694,707,719,731,743,754,766,777,788,798,809,819,
        829,838,848,857,866,874,882,891,898,906,913,920,927,933,939,945,951,956,
        961,965,970,974,978,981,984,987,990,992,994,996,997,998,999,999,1000
    };
    if (d <= 90) return s[d];
    if (d <= 180) return s[180 - d];
    if (d <= 270) return -s[d - 180];
    return -s[360 - d];
}

static int icos(int d) { return isin(d + 90); }

static void draw_line_px(uint32_t *pixels, uint32_t stride, uint32_t w, uint32_t h,
                         int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 >= y0) ? -(y1 - y0) : -(y0 - y1);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        if (x0 >= 0 && (uint32_t)x0 < w && y0 >= 0 && (uint32_t)y0 < h) {
            pixels[y0 * stride + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: 3d\nRotating 3D wireframe cube demo; 'q' or Esc exits.\n");
        print_flush();
        return 0;
    }

    if (gfx_init() != 0) {
        print("3d: failed to initialize graphics framebuffer\n");
        return 1;
    }

    int16_t cube[8][3] = {
        {-100,-100,-100}, {100,-100,-100}, {100,100,-100}, {-100,100,-100},
        {-100,-100,100}, {100,-100,100}, {100,100,100}, {-100,100,100}
    };
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };

    int angle_y = 0;
    int angle_x = 0;
    uint32_t width = gfx_get_width();
    uint32_t height = gfx_get_height();
    uint32_t stride = gfx_get_pitch() / 4;
    int cx = width / 2;
    int cy = height / 2;

    while (1) {
        gfx_event_t event;
        while (gfx_poll_event(&event)) {
            if (event.type == GFX_EVENT_KEY_DOWN) {
                if (event.data1 == 27 || event.data1 == 'q' || event.data1 == 'Q' || event.data1 == 3) {
                    print("\033[2J\033[H");
                    print_flush();
                    return 0;
                }
            }
        }

        uint32_t *pixels = gfx_get_buffer();
        memset(pixels, 0, height * stride * 4);

        angle_y = (angle_y + 3) % 360;
        angle_x = (angle_x + 2) % 360;
        int sy = isin(angle_y), cy_ang = icos(angle_y);
        int sx = isin(angle_x), cx_ang = icos(angle_x);

        for (int i = 0; i < 12; i++) {
            int x1_r = cube[edges[i][0]][0], y1_r = cube[edges[i][0]][1], z1_r = cube[edges[i][0]][2];
            int x2_r = cube[edges[i][1]][0], y2_r = cube[edges[i][1]][1], z2_r = cube[edges[i][1]][2];

            // Rotate Y
            int x1 = (x1_r * cy_ang - z1_r * sy) / 1000;
            int z1 = (x1_r * sy + z1_r * cy_ang) / 1000;
            int x2 = (x2_r * cy_ang - z2_r * sy) / 1000;
            int z2 = (x2_r * sy + z2_r * cy_ang) / 1000;

            // Rotate X
            int y1 = (y1_r * cx_ang - z1 * sx) / 1000;
            z1 = (y1_r * sx + z1 * cx_ang) / 1000;
            int y2 = (y2_r * cx_ang - z2 * sx) / 1000;
            z2 = (y2_r * sx + z2 * cx_ang) / 1000;

            int px1 = cx + (x1 * 450 / (z1 + 500));
            int py1 = cy + (y1 * 450 / (z1 + 500));
            int px2 = cx + (x2 * 450 / (z2 + 500));
            int py2 = cy + (y2 * 450 / (z2 + 500));

            uint32_t edge_col = 0x0000FF00; // Neon green
            if (i >= 8) edge_col = 0x0000FFFF; // Cyan
            draw_line_px(pixels, stride, width, height, px1, py1, px2, py2, edge_col);
        }

        gfx_draw_string(16, 16, "KyroOS 3D Wireframe Demo (q / Esc to quit)", 0xFFFFFFFF, 0xFF000000);
        gfx_flip();
        sleep_ms(16); // ~60 fps
    }

    return 0;
}
