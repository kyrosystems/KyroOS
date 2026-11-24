#include <kyroolib.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_TEXT_LEN 65536
#define SCREEN_ROWS 25
#define SCREEN_COLS 80
#define CUT_BUFFER_LEN 4096

static char filename[128] = "untitled.txt";
static char text[MAX_TEXT_LEN];
static int text_len = 0;
static int cursor = 0;
static int scroll_line = 0;
static char status_msg[128] = "";
static bool modified = false;
static char cut_buffer[CUT_BUFFER_LEN] = "";

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

static int count_total_lines(int *line_starts, int max_starts) {
    int count = 0;
    if (max_starts > 0) line_starts[count++] = 0;
    for (int i = 0; i < text_len && count < max_starts; i++) {
        if (text[i] == '\n') {
            line_starts[count++] = i + 1;
        }
    }
    return count;
}

static void draw_editor(void) {
    // Top Title Bar (Authentic GNU nano 7.2)
    print("\033[H\033[7m");
    char title_left[32] = "  GNU nano 7.2";
    char title_mid[64];
    snprintf(title_mid, sizeof(title_mid), "File: %s", filename);
    const char *mod_str = modified ? "[ Modified ]" : "            ";

    int left_len = strlen(title_left);
    int mid_len = strlen(title_mid);
    int right_len = strlen(mod_str);

    print(title_left);
    int pad1 = (SCREEN_COLS / 2) - left_len - (mid_len / 2);
    if (pad1 < 1) pad1 = 1;
    for (int i = 0; i < pad1; i++) print(" ");
    print(title_mid);
    int printed_so_far = left_len + pad1 + mid_len;
    int pad2 = SCREEN_COLS - printed_so_far - right_len - 2;
    if (pad2 < 1) pad2 = 1;
    for (int i = 0; i < pad2; i++) print(" ");
    print(mod_str);
    print("  \033[0m\n");

    // Line start indexing
    int line_starts[2048];
    int total_lines = count_total_lines(line_starts, 2048);

    int cur_r, cur_c;
    get_cursor_row_col(&cur_r, &cur_c);

    int edit_rows = SCREEN_ROWS - 4; // Title(1) + Status(1) + Help(2) = 4
    if (edit_rows < 1) edit_rows = 1;

    if (cur_r < scroll_line) scroll_line = cur_r;
    if (cur_r >= scroll_line + edit_rows) {
        scroll_line = cur_r - edit_rows + 1;
    }

    // Text Viewport
    for (int l = 0; l < edit_rows; l++) {
        int line_idx = scroll_line + l;
        print("\033[2K"); // Clear entire line
        if (line_idx < total_lines) {
            int start = line_starts[line_idx];
            int end = (line_idx + 1 < total_lines) ? line_starts[line_idx + 1] - 1 : text_len;
            int len = end - start;
            if (len > 0) {
                if (len > SCREEN_COLS) len = SCREEN_COLS;
                char line_buf[256];
                if (len > 250) len = 250;
                memcpy(line_buf, text + start, len);
                line_buf[len] = '\0';
                print(line_buf);
            }
        }
        print("\n");
    }

    // Status / Message Line
    print("\033[2K");
    if (status_msg[0] != '\0') {
        print("  ");
        print(status_msg);
    }
    print("\n");

    // GNU nano 2-row Shortcut Grid
    // Row 1
    print("\033[2K");
    print("\033[7m^G\033[0m Help      ");
    print("\033[7m^O\033[0m WriteOut  ");
    print("\033[7m^W\033[0m Where Is  ");
    print("\033[7m^K\033[0m Cut       ");
    print("\033[7m^T\033[0m Execute   ");
    print("\033[7m^C\033[0m Location  \n");

    // Row 2
    print("\033[2K");
    print("\033[7m^X\033[0m Exit      ");
    print("\033[7m^R\033[0m Read File ");
    print("\033[7m^\\\033[0m Replace   ");
    print("\033[7m^U\033[0m Paste     ");
    print("\033[7m^J\033[0m Justify   ");
    print("\033[7m^_\033[0m Go To Line");

    // Position cursor at editing coordinate
    int screen_row = (cur_r - scroll_line) + 2;
    int screen_col = cur_c + 1;
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "\033[%d;%dH", screen_row, screen_col);
    print(pos_buf);
    print_flush();
}

static bool save_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        snprintf(status_msg, sizeof(status_msg), "[ Error opening '%s' for writing ]", path);
        return false;
    }
    int written = 0;
    if (text_len > 0) {
        written = write(fd, text, text_len);
    }
    close(fd);
    if (written >= 0) {
        int lines = 1;
        for (int i = 0; i < text_len; i++) if (text[i] == '\n') lines++;
        snprintf(status_msg, sizeof(status_msg), "[ Wrote %d lines (%d bytes) to %s ]", lines, text_len, path);
        modified = false;
        return true;
    } else {
        snprintf(status_msg, sizeof(status_msg), "[ Error writing to '%s' ]", path);
        return false;
    }
}

static void prompt_write_out(void) {
    char input_name[128];
    strncpy(input_name, filename, sizeof(input_name) - 1);
    input_name[sizeof(input_name) - 1] = '\0';

    int edit_rows = SCREEN_ROWS - 4;
    int prompt_row = edit_rows + 2;

    char pbuf[32];
    snprintf(pbuf, sizeof(pbuf), "\033[%d;1H\033[2K", prompt_row);
    print(pbuf);
    print("File Name to Write [");
    print(input_name);
    print("]: ");
    print_flush();

    int len = strlen(input_name);
    while (1) {
        char ch = 0;
        if (read(0, &ch, 1) <= 0) continue;
        if (ch == 3 || ch == 27) { // Ctrl+C or Esc
            snprintf(status_msg, sizeof(status_msg), "[ Cancelled ]");
            return;
        }
        if (ch == '\r' || ch == '\n') {
            break;
        }
        if (ch == '\b' || ch == 127) {
            if (len > 0) {
                len--;
                input_name[len] = '\0';
                print("\b \b");
                print_flush();
            }
            continue;
        }
        if (ch >= 32 && ch <= 126 && len < 120) {
            input_name[len++] = ch;
            input_name[len] = '\0';
            char out[2] = {ch, '\0'};
            print(out);
            print_flush();
        }
    }

    if (len > 0) {
        strncpy(filename, input_name, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }
    save_file(filename);
}

static void cut_current_line(void) {
    if (text_len == 0) return;

    int line_start = cursor;
    while (line_start > 0 && text[line_start - 1] != '\n') line_start--;

    int line_end = cursor;
    while (line_end < text_len && text[line_end] != '\n') line_end++;
    if (line_end < text_len && text[line_end] == '\n') line_end++;

    int len = line_end - line_start;
    if (len > 0 && len < CUT_BUFFER_LEN) {
        memcpy(cut_buffer, text + line_start, len);
        cut_buffer[len] = '\0';

        memmove(text + line_start, text + line_end, text_len - line_end);
        text_len -= len;
        text[text_len] = '\0';
        cursor = line_start;
        if (cursor > text_len) cursor = text_len;
        modified = true;
        snprintf(status_msg, sizeof(status_msg), "[ Cut 1 line ]");
    }
}

static void paste_cut_line(void) {
    int clen = strlen(cut_buffer);
    if (clen == 0) {
        snprintf(status_msg, sizeof(status_msg), "[ Cutbuffer is empty ]");
        return;
    }
    if (text_len + clen >= MAX_TEXT_LEN - 1) {
        snprintf(status_msg, sizeof(status_msg), "[ Buffer full ]");
        return;
    }

    memmove(text + cursor + clen, text + cursor, text_len - cursor);
    memcpy(text + cursor, cut_buffer, clen);
    cursor += clen;
    text_len += clen;
    text[text_len] = '\0';
    modified = true;
    snprintf(status_msg, sizeof(status_msg), "[ Pasted 1 line ]");
}

static void show_location(void) {
    int cur_r, cur_c;
    get_cursor_row_col(&cur_r, &cur_c);

    int line_starts[2048];
    int total_lines = count_total_lines(line_starts, 2048);

    int pct_line = total_lines > 0 ? ((cur_r + 1) * 100) / total_lines : 100;
    int pct_char = text_len > 0 ? (cursor * 100) / text_len : 100;

    snprintf(status_msg, sizeof(status_msg),
             "[ line %d/%d (%d%%), col %d, char %d/%d (%d%%) ]",
             cur_r + 1, total_lines, pct_line, cur_c + 1, cursor, text_len, pct_char);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print("Usage: nano [file] (or edit [file])\n");
            print("GNU nano clone for KyroOS.\n");
            print_flush();
            return 0;
        }
        strncpy(filename, argv[1], sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }

    memset(text, 0, sizeof(text));
    text_len = 0;
    cursor = 0;
    scroll_line = 0;
    modified = false;
    status_msg[0] = '\0';

    int fd = open(filename, O_RDONLY);
    if (fd >= 0) {
        int n = read(fd, text, sizeof(text) - 1);
        close(fd);
        if (n > 0) {
            text_len = n;
            text[text_len] = '\0';
            int lines = 1;
            for (int i = 0; i < text_len; i++) if (text[i] == '\n') lines++;
            snprintf(status_msg, sizeof(status_msg), "[ Read %d lines ]", lines);
        }
    } else {
        snprintf(status_msg, sizeof(status_msg), "[ New File ]");
    }

    clear_screen();
    draw_editor();

    while (1) {
        char c = 0;
        int n = read(0, &c, 1);
        if (n <= 0) continue;

        // Ctrl+X: Exit
        if (c == 24) {
            if (modified) {
                int edit_rows = SCREEN_ROWS - 4;
                int prompt_row = edit_rows + 2;
                char pbuf[32];
                snprintf(pbuf, sizeof(pbuf), "\033[%d;1H\033[2K", prompt_row);
                print(pbuf);
                print("Save modified buffer?  (Answering \"No\" will DISCARD changes)  [Y/N/^C]: ");
                print_flush();

                char ans = 0;
                while (read(0, &ans, 1) <= 0);
                if (ans == 'y' || ans == 'Y') {
                    prompt_write_out();
                    break;
                } else if (ans == 'n' || ans == 'N') {
                    break;
                } else {
                    snprintf(status_msg, sizeof(status_msg), "[ Cancelled ]");
                    draw_editor();
                    continue;
                }
            }
            break;
        }

        // Ctrl+O: WriteOut (Save)
        if (c == 15) {
            prompt_write_out();
            draw_editor();
            continue;
        }

        // Ctrl+K: Cut line
        if (c == 11) {
            cut_current_line();
            draw_editor();
            continue;
        }

        // Ctrl+U: Paste line
        if (c == 21) {
            paste_cut_line();
            draw_editor();
            continue;
        }

        // Ctrl+C: Location
        if (c == 3) {
            show_location();
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
                status_msg[0] = '\0';
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
                status_msg[0] = '\0';
            }
            draw_editor();
            continue;
        }

        // Tab: 4 spaces
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
            status_msg[0] = '\0';
            draw_editor();
            continue;
        }

        // Escape sequence (Arrows, Home, End, Delete, PageUp, PageDown)
        if (c == 27) {
            char seq[3];
            if (read(0, &seq[0], 1) > 0 && read(0, &seq[1], 1) > 0) {
                if (seq[0] == '[') {
                    if (seq[1] == 'D') { // Left
                        if (cursor > 0) cursor--;
                    } else if (seq[1] == 'C') { // Right
                        if (cursor < text_len) cursor++;
                    } else if (seq[1] == 'H') { // Home
                        while (cursor > 0 && text[cursor - 1] != '\n') cursor--;
                    } else if (seq[1] == 'F') { // End
                        while (cursor < text_len && text[cursor] != '\n') cursor++;
                    } else if (seq[1] == 'A') { // Up
                        int r, col;
                        get_cursor_row_col(&r, &col);
                        if (r > 0) {
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
                    } else if (seq[1] == '3') { // Delete
                        char tilde = 0;
                        read(0, &tilde, 1);
                        if (cursor < text_len) {
                            memmove(text + cursor, text + cursor + 1, text_len - cursor);
                            text_len--;
                            text[text_len] = '\0';
                            modified = true;
                            status_msg[0] = '\0';
                        }
                    } else if (seq[1] == '5') { // Page Up
                        char tilde = 0;
                        read(0, &tilde, 1);
                        scroll_line -= (SCREEN_ROWS - 4);
                        if (scroll_line < 0) scroll_line = 0;
                    } else if (seq[1] == '6') { // Page Down
                        char tilde = 0;
                        read(0, &tilde, 1);
                        scroll_line += (SCREEN_ROWS - 4);
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
                status_msg[0] = '\0';
            }
            draw_editor();
            continue;
        }
    }

    clear_screen();
    print_flush();
    return 0;
}
