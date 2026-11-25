#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>

typedef enum {
    EVENT_NONE,
    EVENT_KEY_DOWN,
    EVENT_KEY_UP,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_DOWN,
    EVENT_MOUSE_UP,
    EVENT_NET_REPLY
} event_type_t;

typedef struct {
    event_type_t type;
    int32_t data1;
    int32_t data2;
    int32_t data3;
} event_t;

void event_init();
void event_push(event_t event);
int event_pop(event_t* event); // Returns 1 if an event was popped, 0 otherwise
void event_wait(event_t* event); // Blocks until an event is available

#endif // EVENT_H
