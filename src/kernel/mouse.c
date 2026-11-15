#include "mouse.h"
#include "event.h" // For pushing events
#include "fb.h"    // For getting screen dimensions
#include "isr.h"
#include "log.h"
#include "port_io.h"

#define KBC_STATUS_PORT 0x64
#define KBC_CMD_PORT 0x64
#define KBC_DATA_PORT 0x60
#define MOUSE_DATA_PORT 0x60

static int32_t mouse_x = 0;
static int32_t mouse_y = 0;
static uint8_t mouse_cycle = 0;
static uint8_t mouse_raw[3];
static uint8_t last_button_state = 0;

// Helper to wait for the keyboard controller to be ready
static void mouse_wait(uint8_t type) {
  uint32_t timeout = 100000;
  if (type == 0) { // Wait for input buffer to be empty
    while (timeout-- && (inb(KBC_STATUS_PORT) & 2))
      ;
  } else { // Wait for output buffer to be full
    while (timeout-- && !(inb(KBC_STATUS_PORT) & 1))
      ;
  }
}

// Helper to write to the mouse device
static void mouse_write(uint8_t data) {
  mouse_wait(0);
  outb(KBC_CMD_PORT, 0xD4); // Tell KBC we are sending a command to the mouse
  mouse_wait(0);
  outb(KBC_DATA_PORT, data);
}

// Helper to read from the mouse device
static uint8_t mouse_read() {
  mouse_wait(1);
  return inb(KBC_DATA_PORT);
}

// The main IRQ12 handler
void mouse_handler(struct registers *regs) {
  (void)regs; // Suppress unused parameter warning

  uint8_t status = inb(KBC_CMD_PORT);
  if (!(status & 0x20))
    return; // Check if data is from mouse

  uint8_t b = inb(MOUSE_DATA_PORT);

  switch (mouse_cycle) {
  case 0:
    // Packet byte 0: bit 3 must be 1, bits 6 and 7 (overflow) should be 0
    if ((b & 0x08) != 0 && (b & 0xC0) == 0) {
      mouse_raw[0] = b;
      mouse_cycle = 1;
    }
    break;
  case 1:
    mouse_raw[1] = b;
    mouse_cycle = 2;
    break;
  case 2:
    mouse_raw[2] = b;
    mouse_cycle = 0;

    // Decode 9-bit signed movement with sign extension
    int32_t delta_x = (int32_t)mouse_raw[1];
    int32_t delta_y = (int32_t)mouse_raw[2];
    if (mouse_raw[0] & 0x10) delta_x |= 0xFFFFFF00;
    if (mouse_raw[0] & 0x20) delta_y |= 0xFFFFFF00;

    mouse_x += delta_x;
    mouse_y -= delta_y; // PS/2 Y is positive upwards, screen Y increases downwards

    const fb_info_t *info = fb_get_info();
    if (mouse_x < 0)
      mouse_x = 0;
    if (mouse_y < 0)
      mouse_y = 0;
    if (info && info->width > 0 && info->height > 0) {
      if (mouse_x >= (int32_t)info->width)
        mouse_x = info->width - 1;
      if (mouse_y >= (int32_t)info->height)
        mouse_y = info->height - 1;
    }

    event_t event = {0};
    event.type = EVENT_MOUSE_MOVE;
    event.data1 = mouse_x;
    event.data2 = mouse_y;
    event_push(event);

    // Button presses
    uint8_t current_button_state = mouse_raw[0] & 0x07;
    if (current_button_state != last_button_state) {
      event_t btn_event;
      // Check left button (bit 0)
      if ((current_button_state & 1) && !(last_button_state & 1)) {
        btn_event.type = EVENT_MOUSE_DOWN;
        btn_event.data1 = 1;
        btn_event.data2 = mouse_x;
        btn_event.data3 = mouse_y;
        event_push(btn_event);
      } else if (!(current_button_state & 1) && (last_button_state & 1)) {
        btn_event.type = EVENT_MOUSE_UP;
        btn_event.data1 = 1;
        btn_event.data2 = mouse_x;
        btn_event.data3 = mouse_y;
        event_push(btn_event);
      }

      // Check right button (bit 1)
      if ((current_button_state & 2) && !(last_button_state & 2)) {
        btn_event.type = EVENT_MOUSE_DOWN;
        btn_event.data1 = 2;
        btn_event.data2 = mouse_x;
        btn_event.data3 = mouse_y;
        event_push(btn_event);
      } else if (!(current_button_state & 2) && (last_button_state & 2)) {
        btn_event.type = EVENT_MOUSE_UP;
        btn_event.data1 = 2;
        btn_event.data2 = mouse_x;
        btn_event.data3 = mouse_y;
        event_push(btn_event);
      }

      // Check middle button (bit 2)
      if ((current_button_state & 4) && !(last_button_state & 4)) {
        btn_event.type = EVENT_MOUSE_DOWN;
        btn_event.data1 = 3;
        btn_event.data2 = mouse_x;
        btn_event.data3 = mouse_y;
        event_push(btn_event);
      } else if (!(current_button_state & 4) && (last_button_state & 4)) {
        btn_event.type = EVENT_MOUSE_UP;
        btn_event.data1 = 3;
        btn_event.data2 = mouse_x;
        btn_event.data3 = mouse_y;
        event_push(btn_event);
      }

      last_button_state = current_button_state;
    }
    break;
  }
}

void mouse_init() {
  mouse_wait(0);
  outb(KBC_CMD_PORT, 0xA8);

  mouse_wait(0);
  outb(KBC_CMD_PORT, 0x20);
  mouse_wait(1);
  uint8_t status = inb(KBC_DATA_PORT);
  status |= 2;
  status &= ~0x20;
  mouse_wait(0);
  outb(KBC_CMD_PORT, 0x60);
  mouse_wait(0);
  outb(KBC_DATA_PORT, status);

  mouse_write(0xF6);
  mouse_read();
  mouse_write(0xF4);
  mouse_read();

  register_irq_handler(12, mouse_handler);
  const fb_info_t *fbi = fb_get_info();
  if (fbi && fbi->width > 0 && fbi->height > 0) {
    mouse_x = fbi->width / 2;
    mouse_y = fbi->height / 2;
  } else {
    mouse_x = 512;
    mouse_y = 384;
  }
  klog(LOG_INFO, "PS/2 Mouse initialized at (%d, %d).", mouse_x, mouse_y);
}

void mouse_get_position(int32_t *x, int32_t *y) {
  *x = mouse_x;
  *y = mouse_y;
}

void mouse_set_position(int32_t x, int32_t y) {
  const fb_info_t *info = fb_get_info();
  if (info && info->width > 0 && info->height > 0) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int32_t)info->width) x = info->width - 1;
    if (y >= (int32_t)info->height) y = info->height - 1;
  }
  mouse_x = x;
  mouse_y = y;
}
