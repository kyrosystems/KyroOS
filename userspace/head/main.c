#include <kyroolib.h>

#define MAX_LINE 1024

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
        print("Usage: head [-n N] <file>\n");
        return 1;
    }

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        print("head: cannot open '");
        print(filename);
        print("'\n");
        return 1;
    }

    char line[MAX_LINE];
    int pos = 0;
    char c;
    int lines_printed = 0;

    while (read(fd, &c, 1) > 0 && lines_printed < lines_to_print) {
        if (c == '\n' || pos >= MAX_LINE - 1) {
            if (c == '\n') {
                line[pos] = '\0';
            } else {
                line[pos] = c;
                pos++;
                continue;
            }

            print(line);
            print("\n");
            lines_printed++;
            pos = 0;
        } else {
            line[pos++] = c;
        }
    }

    close(fd);
    return 0;
}
