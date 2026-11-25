#ifndef KYRO_DISPLAY_H
#define KYRO_DISPLAY_H
#include <stdint.h>
#define DISPLAY_MAX_SURFACES 8
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 320
/* Local software compositor protocol. Existing framebuffer ABI is unchanged. */
enum { DISPLAY_REGISTER = 1, DISPLAY_CREATE, DISPLAY_LIST, DISPLAY_READ,
       DISPLAY_PRESENT, DISPLAY_SEND, DISPLAY_MODE, DISPLAY_MODE_INFO };
typedef struct {
    uint32_t pid, width, height, pitch;
    uint64_t sequence;
    char title[32];
} display_surface_info_t;
typedef struct {
    uint32_t width, height, supported, reserved;
    uint64_t capacity;
    char driver[32];
} display_mode_t;
int video_mode_info(display_mode_t *info);
int video_set_mode(unsigned width, unsigned height);
typedef struct { void *pixels; uint64_t bytes; } display_read_t;
typedef struct { uint32_t type; int32_t data1, data2, data3; } display_event_t;
#ifdef KYRO_KERNEL_DISPLAY
#include "thread.h"
long display_control(uint64_t op, void *data, uint64_t arg);
int display_poll(thread_t *thread, void *event); /* -1 = not a client */
int display_active(void);
int display_is_server(thread_t *thread);
void display_release(thread_t *thread);
#endif
#endif
