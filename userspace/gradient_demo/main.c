#include <kyroolib.h>
#include <kyroos_gfx.h>

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        print("Usage: gradient_demo\nAnimated color gradient; Esc or q exits.\n");
        print_flush();
        return 0;
    }
    if (gfx_init() != 0) return 1;
    uint32_t phase = 0;
    for (;;) {
        gfx_event_t event;
        while (gfx_poll_event(&event))
            if (event.type == GFX_EVENT_KEY_DOWN &&
                (event.data1 == 27 || event.data1 == 'q' || event.data1 == 3)) return 0;
        uint32_t *pixels = gfx_get_buffer();
        uint32_t stride = gfx_get_pitch() / 4;
        for (uint32_t y = 0; y < gfx_get_height(); y++)
            for (uint32_t x = 0; x < gfx_get_width(); x++)
                pixels[y * stride + x] = 0xff000000 | (((x + phase) & 255) << 16) |
                    (((y + phase) & 255) << 8) | ((x + y + phase) & 255);
        gfx_draw_string(12, 12, "Color cycling - Esc / q to close", 0xffffffff, 0xff18202a);
        gfx_flip();
        phase += 2;
        sleep_ms(20);
    }
}
