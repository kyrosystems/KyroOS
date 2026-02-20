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
static int8_t mouse_byte[3];
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

  switch (mouse_cycle) {
  case 0:
    mouse_byte[0] = inb(MOUSE_DATA_PORT);
    if (mouse_byte[0] & 0x08) {
      mouse_cycle++;
    }
    break;
  case 1:
    mouse_byte[1] = inb(MOUSE_DATA_PORT);
    mouse_cycle++;
    break;
  case 2:
    mouse_byte[2] = inb(MOUSE_DATA_PORT);
    mouse_cycle = 0;

    int8_t delta_x = mouse_byte[1];
    int8_t delta_y = mouse_byte[2];

    if (mouse_byte[0] & 0x20)
      delta_y = -delta_y;
    if (mouse_byte[0] & 0x10)
      delta_x = -delta_x;

    mouse_x += delta_x;
    mouse_y -= delta_y;

    const fb_info_t *info = fb_get_info();
    if (mouse_x < 0)
      mouse_x = 0;
    if (mouse_y < 0)
      mouse_y = 0;
    if (info) {
      if (mouse_x >= (int32_t)info->width)
        mouse_x = info->width - 1;
      if (mouse_y >= (int32_t)info->height)
        mouse_y = info->height - 1;
    }

    event_t event;
    event.type = EVENT_MOUSE_MOVE;
    event.data1 = mouse_x;
    event.data2 = mouse_y;
    event_push(event);

    // Button presses
    uint8_t current_button_state = mouse_byte[0] & 0x07;
    if (current_button_state != last_button_state) {
      event_t btn_event;
      // Check left button
      if ((current_button_state & 1) && !(last_button_state & 1)) {
        btn_event.type = EVENT_MOUSE_DOWN;
        btn_event.data1 = 1;
        event_push(btn_event);
      } else if (!(current_button_state & 1) && (last_button_state & 1)) {
        btn_event.type = EVENT_MOUSE_UP;
        btn_event.data1 = 1;
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
  klog(LOG_INFO, "PS/2 Mouse initialized.");
}

void mouse_get_position(int32_t *x, int32_t *y) {
  *x = mouse_x;
  *y = mouse_y;
}
