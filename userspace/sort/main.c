#include <kyroolib.h>

#define MAX_LINES 1000
#define MAX_LINE_LEN 256

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: sort [file]\n");
        return 1;
    }

    const char *filename = argv[1];
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        print("sort: cannot open '");
        print(filename);
        print("'\n");
        return 1;
    }

    // Read all lines
    static char lines[MAX_LINES][MAX_LINE_LEN];
    int line_count = 0;

    char line[MAX_LINE_LEN];
    int pos = 0;
    char c;

    while (read(fd, &c, 1) > 0 && line_count < MAX_LINES) {
        if (c == '\n' || pos >= MAX_LINE_LEN - 1) {
            if (c == '\n') {
                line[pos] = '\0';
            } else {
                line[pos] = c;
                pos++;
                continue;
            }

            if (pos > 0) {
                strcpy(lines[line_count], line);
                line_count++;
            }
            pos = 0;
        } else {
            line[pos++] = c;
        }
    }
    close(fd);

    // Simple bubble sort
    for (int i = 0; i < line_count - 1; i++) {
        for (int j = 0; j < line_count - i - 1; j++) {
            if (strcmp(lines[j], lines[j + 1]) > 0) {
                char temp[MAX_LINE_LEN];
                strcpy(temp, lines[j]);
                strcpy(lines[j], lines[j + 1]);
                strcpy(lines[j + 1], temp);
            }
        }
    }

    // Print sorted lines
    for (int i = 0; i < line_count; i++) {
        print(lines[i]);
        print("\n");
    }

    return 0;
}
