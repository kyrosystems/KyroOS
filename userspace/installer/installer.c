#include "tui/tui.h"
#include <kyroolib.h>

int main() {
  fb_info_t fb_info;
  if (tui_init(&fb_info) != 0) {
    // Cannot initialize graphics
    return -1;
  }

  // Colors
  uint32_t bg_color = 0x0000AA;         // Blue
  uint32_t title_bg_color = 0xAAAAAA;   // Gray
  uint32_t text_color = 0xFFFFFF;       // White
  uint32_t title_text_color = 0x000000; // Black

  tui_clear_screen(&fb_info, bg_color);

  // Draw title bar
  tui_draw_box(&fb_info, 0, 0, fb_info.width, 24, title_bg_color);
  tui_draw_text(&fb_info, 5, 4, "KyroOS Installer", title_text_color);

  // Draw main window
  uint32_t win_x = 50;
  uint32_t win_y = 50;
  uint32_t win_w = fb_info.width - 100;
  uint32_t win_h = fb_info.height - 100;
  tui_draw_box(&fb_info, win_x, win_y, win_w, win_h, title_bg_color);

  tui_draw_text(&fb_info, win_x + 10, win_y + 10,
                "Welcome to the KyroOS Installer!", text_color);
  tui_draw_text(
      &fb_info, win_x + 10, win_y + 40,
      "This utility will guide you through installing KyroOS on your machine.",
      text_color);

  // Draw a button
  uint32_t btn_w = 100;
  uint32_t btn_h = 30;
  uint32_t btn_x = (fb_info.width / 2) - (btn_w / 2);
  uint32_t btn_y = fb_info.height - 70;
  tui_draw_box(&fb_info, btn_x, btn_y, btn_w, btn_h, 0x00AA00);
  tui_draw_text(&fb_info, btn_x + 25, btn_y + 7, "Next >", text_color);

  // Event loop
  while (1) {
    event_t event;
    while (input_poll_event(&event)) {
      if (event.type == EVENT_KEY_DOWN) {
        if ((char)event.data1 == 'q') {
          // Exit
          return 0;
        }
        // Simple button click simulation for now (until full mouse support)
        // If 'n' is pressed, pretend "Next" was clicked?
        // For now, just 'q' to exit is enough to prove it runs.
      }
    }

    // Redraw or update if needed
    // For this simple installer, static draw is fine for now.

    // Small delay to prevent 100% CPU usage
    syscall(SYS_GET_TICKS, 0, 0, 0); // access ticks as a slight delay/yield
  }

  return 0;
}
