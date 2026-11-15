#define KYRO_KERNEL_DISPLAY
#include "display.h"
#include "heap.h"
#include "kstring.h"
#include "uaccess.h"
#include "scheduler.h"
#include "errno.h"

#define SURFACE_BYTES (DISPLAY_WIDTH * DISPLAY_HEIGHT * 4)
#define INPUT_COUNT 64
typedef struct {
    thread_t *owner;
    display_surface_info_t info;
    void *pixels;
    display_event_t events[INPUT_COUNT];
    unsigned head, count;
} surface_t;
static thread_t *server;
static surface_t surfaces[DISPLAY_MAX_SURFACES];

int display_active(void) { return server != 0; }
int display_is_server(thread_t *thread) { return server && server == thread; }
static surface_t *find(thread_t *owner, uint64_t pid) {
    for (int i = 0; i < DISPLAY_MAX_SURFACES; i++)
        if (surfaces[i].owner && ((owner && surfaces[i].owner == owner) ||
            (!owner && surfaces[i].info.pid == pid))) return &surfaces[i];
    return 0;
}
long display_control(uint64_t op, void *data, uint64_t arg) {
    thread_t *self = get_current_thread();
    if (!self) return -EINVAL;
    if (op == DISPLAY_REGISTER) {
        if (server && server != self) return -EBUSY;
        /* Do not allow another user's process to capture an existing session. */
        server = self;
        console_focus = self;
        return 0;
    }
    if (op == DISPLAY_CREATE) {
        if (!server || server == self) return -ENODEV;
        if (self->uid != server->uid) return -EPERM;
        if (validate_user_write(data, sizeof(display_surface_info_t))) return -EFAULT;
        surface_t *s = find(self, 0);
        if (!s) {
            for (int i = 0; i < DISPLAY_MAX_SURFACES; i++)
                if (!surfaces[i].owner) { s = &surfaces[i]; break; }
            if (!s) return -ENOMEM;
            void *pixels = kmalloc(SURFACE_BYTES);
            if (!pixels) return -ENOMEM;
            memset(s, 0, sizeof(*s));
            memset(pixels, 0, SURFACE_BYTES);
            s->pixels = pixels;
            s->owner = self;
            s->info.pid = self->id;
            s->info.width = DISPLAY_WIDTH;
            s->info.height = DISPLAY_HEIGHT;
            s->info.pitch = DISPLAY_WIDTH * 4;
            strncpy(s->info.title, self->name, sizeof(s->info.title) - 1);
        }
        memcpy(data, &s->info, sizeof(s->info));
        return 0;
    }
    surface_t *s = find(self, 0);
    if (op == DISPLAY_PRESENT) {
        if (!server || !s) return -ENODEV;
        if (validate_user_pointer(data, SURFACE_BYTES)) return -EFAULT;
        memcpy(s->pixels, data, SURFACE_BYTES);
        s->info.sequence++;
        return 0;
    }
    if (server != self) return -EPERM;
    if (op == DISPLAY_MODE_INFO || op == DISPLAY_MODE) {
        if (validate_user_write(data, sizeof(display_mode_t))) return -EFAULT;
        display_mode_t info;
        memcpy(&info, data, sizeof(info));
        if (op == DISPLAY_MODE) {
            int rc = video_set_mode(info.width, info.height);
            if (rc) return rc;
        }
        video_mode_info(&info);
        memcpy(data, &info, sizeof(info));
        return 0;
    }
    if (op == DISPLAY_LIST) {
        if (validate_user_write(data, sizeof(display_surface_info_t) * DISPLAY_MAX_SURFACES)) return -EFAULT;
        display_surface_info_t *list = data;
        for (int i = 0; i < DISPLAY_MAX_SURFACES; i++) {
            memset(&list[i], 0, sizeof(list[i]));
            if (surfaces[i].owner) list[i] = surfaces[i].info;
        }
        return 0;
    }
    s = find(0, arg);
    if (!s) return -ENOENT;
    if (op == DISPLAY_READ) {
        if (validate_user_pointer(data, sizeof(display_read_t))) return -EFAULT;
        display_read_t req;
        memcpy(&req, data, sizeof(req));
        if (req.bytes < SURFACE_BYTES) return -EINVAL;
        if (validate_user_write(req.pixels, SURFACE_BYTES)) return -EFAULT;
        memcpy(req.pixels, s->pixels, SURFACE_BYTES);
        return 0;
    }
    if (op == DISPLAY_SEND) {
        if (validate_user_pointer(data, sizeof(display_event_t))) return -EFAULT;
        display_event_t ev;
        memcpy(&ev, data, sizeof(ev));
        /* Coalesce motion, without discarding button/key ordering. */
        if (ev.type == 3 && s->count &&
            s->events[(s->head + s->count - 1) % INPUT_COUNT].type == 3) {
            s->events[(s->head + s->count - 1) % INPUT_COUNT] = ev;
            return 0;
        }
        if (s->count == INPUT_COUNT) return -EAGAIN;
        s->events[(s->head + s->count++) % INPUT_COUNT] = ev;
        return 0;
    }
    return -EINVAL;
}
int display_poll(thread_t *thread, void *event) {
    surface_t *s = find(thread, 0);
    if (!s) return -1;
    if (!s->count) return 0;
    memcpy(event, &s->events[s->head], sizeof(display_event_t));
    s->head = (s->head + 1) % INPUT_COUNT;
    s->count--;
    return 1;
}
void display_release(thread_t *thread) {
    if (server == thread) server = 0;
    surface_t *s = find(thread, 0);
    if (s) {
        kfree(s->pixels);
        memset(s, 0, sizeof(*s));
    }
}
