#include <kyroolib.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_TEXT_LEN 65536
#define SCREEN_ROWS 24
#define SCREEN_COLS 80

static char filename[128] = "untitled.txt";
static char text[MAX_TEXT_LEN];
static int text_len = 0;
static int cursor = 0;
static int scroll_line = 0;
static char status_msg[128] = "Press Ctrl+O to save, Ctrl+X to exit";
static bool modified = false;

static void clear_screen(void) {
    print("\033[2J\033[H");
}

static void get_cursor_row_col(int *out_row, int *out_col) {
    int r = 0;
    int c = 0;
    for (int i = 0; i < cursor && i < text_len; i++) {
        if (text[i] == '\n') {
            r++;
            c = 0;
        } else {
            c++;
        }
    }
    *out_row = r;
    *out_col = c;
}

static void draw_editor(void) {
    print("\033[H"); // home

    // Header
    print("\033[7m  KyroEdit 2.0 - ");
    print(filename);
    if (modified) print(" *");
    print("  |  ^O: Save  ^X: Exit \033[0m\n");

    // Count lines and find lines to draw
    int line_starts[1024];
    int total_lines = 0;
    line_starts[total_lines++] = 0;
    for (int i = 0; i < text_len && total_lines < 1024; i++) {
        if (text[i] == '\n') {
            line_starts[total_lines++] = i + 1;
        }
    }

    int cur_r, cur_c;
    get_cursor_row_col(&cur_r, &cur_c);
    if (cur_r < scroll_line) scroll_line = cur_r;
    if (cur_r >= scroll_line + (SCREEN_ROWS - 2)) {
        scroll_line = cur_r - (SCREEN_ROWS - 2) + 1;
    }

    int max_draw_lines = SCREEN_ROWS - 2;
    for (int l = 0; l < max_draw_lines; l++) {
        int line_idx = scroll_line + l;
        print("\033[2K"); // Clear entire line
        if (line_idx < total_lines) {
            int start = line_starts[line_idx];
            int end = (line_idx + 1 < total_lines) ? line_starts[line_idx + 1] - 1 : text_len;
            char line_buf[256];
            int len = end - start;
            if (len > 250) len = 250;
            if (len > 0) {
                memcpy(line_buf, text + start, len);
                line_buf[len] = '\0';
                print(line_buf);
            }
        } else {
            print("\033[90m~\033[0m");
        }
        print("\n");
    }

    // Status bar
    print("\033[7m ");
    print(status_msg);
    char stats[64];
    snprintf(stats, sizeof(stats), "  |  Ln %d, Col %d  |  Bytes: %d ", cur_r + 1, cur_c + 1, text_len);
    print(stats);
    print("\033[0m\033[K");

    // Position cursor on screen
    int screen_row = (cur_r - scroll_line) + 2;
    int screen_col = cur_c + 1;
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "\033[%d;%dH", screen_row, screen_col);
    print(pos_buf);
    print_flush();
}

static void save_file(void) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        snprintf(status_msg, sizeof(status_msg), "Error: cannot open '%s' for writing", filename);
        return;
    }
    int written = 0;
    if (text_len > 0) {
        written = write(fd, text, text_len);
    }
    close(fd);
    if (written >= 0) {
        snprintf(status_msg, sizeof(status_msg), "[Saved %d bytes to %s]", text_len, filename);
        modified = false;
    } else {
        snprintf(status_msg, sizeof(status_msg), "Error writing to '%s'", filename);
    }
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print("Usage: edit [file]\n");
            print("Visual terminal text editor for KyroOS.\n");
            print_flush();
            return 0;
        }
        strncpy(filename, argv[1], sizeof(filename) - 1);
    }

    memset(text, 0, sizeof(text));
    text_len = 0;
    cursor = 0;
    modified = false;

    // Try opening existing file
    int fd = open(filename, O_RDONLY);
    if (fd >= 0) {
        int n = read(fd, text, sizeof(text) - 1);
        close(fd);
        if (n > 0) {
            text_len = n;
            text[text_len] = '\0';
            snprintf(status_msg, sizeof(status_msg), "Loaded %d bytes from '%s'", text_len, filename);
        }
    } else {
        snprintf(status_msg, sizeof(status_msg), "New file: '%s'", filename);
    }

    clear_screen();
    draw_editor();

    while (1) {
        char c = 0;
        int n = read(0, &c, 1);
        if (n <= 0) continue;

        // Ctrl+X or Ctrl+Q: Exit
        if (c == 24 || c == 17) {
            break;
        }

        // Ctrl+O or Ctrl+S: Save
        if (c == 15 || c == 19) {
            save_file();
            draw_editor();
            continue;
        }

        // Backspace
        if (c == '\b' || c == 127 || c == 8) {
            if (cursor > 0) {
                memmove(text + cursor - 1, text + cursor, text_len - cursor);
                cursor--;
                text_len--;
                text[text_len] = '\0';
                modified = true;
            }
            draw_editor();
            continue;
        }

        // Enter
        if (c == '\n' || c == '\r') {
            if (text_len < MAX_TEXT_LEN - 1) {
                memmove(text + cursor + 1, text + cursor, text_len - cursor);
                text[cursor++] = '\n';
                text_len++;
                text[text_len] = '\0';
                modified = true;
            }
            draw_editor();
            continue;
        }

        // Tab
        if (c == '\t') {
            for (int s = 0; s < 4; s++) {
                if (text_len < MAX_TEXT_LEN - 1) {
                    memmove(text + cursor + 1, text + cursor, text_len - cursor);
                    text[cursor++] = ' ';
                    text_len++;
                    text[text_len] = '\0';
                    modified = true;
                }
            }
            draw_editor();
            continue;
        }

        // Escape sequence (Arrows)
        if (c == 27) {
            char seq[2];
            if (read(0, &seq[0], 1) > 0 && read(0, &seq[1], 1) > 0) {
                if (seq[0] == '[') {
                    if (seq[1] == 'D') { // Left
                        if (cursor > 0) cursor--;
                    } else if (seq[1] == 'C') { // Right
                        if (cursor < text_len) cursor++;
                    } else if (seq[1] == 'A') { // Up
                        int r, col;
                        get_cursor_row_col(&r, &col);
                        if (r > 0) {
                            // Find position in previous line
                            int target_r = r - 1;
                            int curr_r = 0, curr_c = 0;
                            for (int i = 0; i <= text_len; i++) {
                                if (curr_r == target_r && curr_c == col) { cursor = i; break; }
                                if (i == text_len || text[i] == '\n') {
                                    if (curr_r == target_r) { cursor = i; break; }
                                    curr_r++;
                                    curr_c = 0;
                                } else {
                                    curr_c++;
                                }
                            }
                        }
                    } else if (seq[1] == 'B') { // Down
                        int r, col;
                        get_cursor_row_col(&r, &col);
                        int target_r = r + 1;
                        int curr_r = 0, curr_c = 0;
                        for (int i = 0; i <= text_len; i++) {
                            if (curr_r == target_r && curr_c == col) { cursor = i; break; }
                            if (i == text_len || text[i] == '\n') {
                                if (curr_r == target_r) { cursor = i; break; }
                                curr_r++;
                                curr_c = 0;
                            } else {
                                curr_c++;
                            }
                        }
                    }
                }
            }
            draw_editor();
            continue;
        }

        // Regular printable character
        if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
            if (text_len < MAX_TEXT_LEN - 1) {
                memmove(text + cursor + 1, text + cursor, text_len - cursor);
                text[cursor++] = c;
                text_len++;
                text[text_len] = '\0';
                modified = true;
            }
            draw_editor();
            continue;
        }
    }

    clear_screen();
    print_flush();
    return 0;
}
