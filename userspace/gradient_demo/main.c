#include <kyroolib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// Max length for FPS string output
#define FPS_STR_LEN 32

// Placeholder function for gradient (no-op as framebuffer is disabled)
static void draw_gradient_serial_only(uint32_t frame_count) {
    (void)frame_count; // Suppress unused warning
    // In a graphical context, this would draw the gradient.
    // For serial-only, it's a no-op for now.
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // user_fb_info_t fb_info; // Not needed for serial only
    // if (gfx_get_fb_info(&fb_info) != 0) { // Not needed for serial only
    //     print("gradient_demo: Failed to get framebuffer info (as expected in serial mode).\n");
    //     // Continue anyway, as we are doing serial output
    // }
    
    // if (fb_info.buffer == NULL) { // Not needed for serial only
    //     print("gradient_demo: Framebuffer buffer is NULL (as expected in serial mode).\n");
    //     // Continue anyway
    // }

    uint32_t frame_count = 0;
    uint64_t last_tick = get_ticks();
    uint32_t frames_this_second = 0;
    uint32_t fps = 0;

    char fps_buf[FPS_STR_LEN]; // Buffer for FPS string

    print("gradient_demo: Starting gradient simulation. Press Ctrl+C to exit (serial console).\n");

    while (true) {
        draw_gradient_serial_only(frame_count++); // Call placeholder
        
        // FPS calculation
        uint64_t current_tick = get_ticks();
        frames_this_second++;
        if (current_tick - last_tick >= 100) { // Assuming 100 ticks per second (10ms per tick)
            fps = frames_this_second;
            frames_this_second = 0;
            last_tick = current_tick;
            
            // Print FPS to serial
            sprintf(fps_buf, "FPS: %u\n", fps);
            print(fps_buf);
        }

        // Event handling for Ctrl+C
        event_t ev;
        // Check if there's an event without blocking
        int event_available = input_poll_event(&ev); 
        if (event_available) {
            if (ev.type == EVENT_KEY_DOWN) {
                // Left Ctrl scancode: 0x1D
                // 'c' scancode: 0x2E
                if (ev.data3 == 1 && ev.data2 == 0x2E) { // ev.data3 == 1 means Ctrl is pressed
                    print("gradient_demo: Exiting on Ctrl+C.\n");
                    exit(0);
                }
            }
        }
        // Minimal delay to prevent busy-waiting
        // thread_yield(); // If thread_yield() syscall is available
    }

    return 0;
}