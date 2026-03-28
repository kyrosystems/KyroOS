#include <kyroolib.h>

#define MAX_LINE 1024
#define MAX_LINES 100

int main(int argc, char **argv) {
    int lines_to_print = 10; // Default
    const char *filename = NULL;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            lines_to_print = 0;
            const char *n = argv[i + 1];
            for (int j = 0; n[j] != '\0'; j++) {
                if (n[j] >= '0' && n[j] <= '9') {
                    lines_to_print = lines_to_print * 10 + (n[j] - '0');
                }
            }
            i++;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        print("Usage: tail [-n N] <file>\n");
        return 1;
    }

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        print("tail: cannot open '");
        print(filename);
        print("'\n");
        return 1;
    }

    // Store all lines in buffer
    static char lines[MAX_LINES][MAX_LINE];
    int line_count = 0;

    char line_buf[MAX_LINE];
    int pos = 0;
    char c;

    while (read(fd, &c, 1) > 0) {
        if (c == '\n' || pos >= MAX_LINE - 1) {
            if (c == '\n') {
                line_buf[pos] = '\0';
            } else {
                line_buf[pos] = c;
                pos++;
                continue;
            }

            if (line_count < MAX_LINES) {
                strcpy(lines[line_count], line_buf);
                line_count++;
            }
            pos = 0;
        } else {
            line_buf[pos++] = c;
        }
    }
    close(fd);

    // Print last N lines
    int start = (line_count > lines_to_print) ? (line_count - lines_to_print) : 0;
    for (int i = start; i < line_count; i++) {
        print(lines[i]);
        print("\n");
    }

    return 0;
}
