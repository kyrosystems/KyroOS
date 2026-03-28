#include "editor.h"
#include "fb.h"
#include "log.h"
#include "keyboard.h"
#include "vfs.h"
#include "heap.h"
#include "kstring.h"
#include "event.h"

#define EDITOR_BUFFER_SIZE 4096

void editor_draw_status_bar(const char *filename) {
    // Hacky status bar: print at bottom?
    // For now just clear screen and print at top
    console_clear();
    klog_print_str("EDIT: ", false);
    klog_print_str(filename, false);
    klog_print_str(" | Ctrl+S: Save | Ctrl+Q: Quit\n", false);
    klog_print_str("----------------------------------------------------\n", true);
}

void editor_main(const char *path) {
    char *buffer = (char *)kmalloc(EDITOR_BUFFER_SIZE);
    if (!buffer) {
        klog_print_str("Editor: Out of memory.\n", true);
        return;
    }
    memset(buffer, 0, EDITOR_BUFFER_SIZE);
    int buf_len = 0;

    // Load file if exists
    vfs_node_t *node = vfs_resolve_path(vfs_root, path);
    if (node) {
        int read = vfs_read(node, 0, EDITOR_BUFFER_SIZE - 1, (uint8_t*)buffer);
        if (read > 0) {
            buf_len = read;
            buffer[buf_len] = '\0';
        }
    }

    editor_draw_status_bar(path);
    klog_print_str(buffer, true);

    while (1) {
        event_t ev;
        event_wait(&ev);

        if (ev.type == EVENT_KEY_DOWN) {
            char c = (char)ev.data1;
            uint8_t scancode = (uint8_t)ev.data2;
            
            // Check for Ctrl keys (simplification: assume Ctrl is held if we track it, 
            // but event_t doesn't pass modifiers directly unless we query keyboard state)
            // We can check global keyboard state if exposed, or rely on scancode.
            // Scancode 0x1F = S, 0x10 = Q. 
            // We need to know if Ctrl is pressed.
            bool ctrl = keyboard_is_ctrl_pressed();

            if (ctrl && scancode == 0x1F) { // Ctrl+S
                vfs_node_t *fnode = vfs_resolve_path(vfs_root, path);
                if (!fnode) {
                    // Create if not exists
                    // Need parent
                    // Simplified: just try creating in root or cwd logic
                    // For now, fail if not exists or rely on shell touch first
                    klog_print_str("\n[Saving not fully implemented for new files]\n", true);
                } else {
                    vfs_write(fnode, 0, buf_len, (uint8_t*)buffer);
                    klog_print_str("\n[Saved]\n", true);
                }
                // Redraw
                editor_draw_status_bar(path);
                klog_print_str(buffer, true);
            }
            else if (ctrl && scancode == 0x10) { // Ctrl+Q
                break;
            }
            else if (c == '\b') {
                if (buf_len > 0) {
                    buf_len--;
                    buffer[buf_len] = '\0';
                    // Redraw entire buffer (inefficient but works)
                    editor_draw_status_bar(path);
                    klog_print_str(buffer, true);
                }
            }
            else if (c >= 32 || c == '\n') {
                if (buf_len < EDITOR_BUFFER_SIZE - 1) {
                    buffer[buf_len++] = c;
                    buffer[buf_len] = '\0';
                    klog_putchar(c);
                    fb_flush();
                }
            }
        }
    }

    kfree(buffer);
    console_clear();
}
