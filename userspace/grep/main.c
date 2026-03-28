#include <kyroolib.h>

#define MAX_LINE 1024

int main(int argc, char **argv) {
    if (argc < 3) {
        print("Usage: grep <pattern> <file>\n");
        return 1;
    }

    const char *pattern = argv[1];
    const char *filename = argv[2];

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        print("grep: cannot open '");
        print(filename);
        print("'\n");
        return 1;
    }

    char line[MAX_LINE];
    int line_pos = 0;
    char c;
    int match = 0;

    while (read(fd, &c, 1) > 0) {
        if (c == '\n' || line_pos >= MAX_LINE - 1) {
            if (c == '\n') {
                line[line_pos] = '\0';
            } else {
                line[line_pos] = c;
                line_pos++;
                continue;
            }

            // Simple substring search
            int pattern_len = strlen(pattern);
            int line_len = strlen(line);
            int found = 0;

            for (int i = 0; i <= line_len - pattern_len; i++) {
                int j;
                for (j = 0; j < pattern_len; j++) {
                    if (line[i + j] != pattern[j]) break;
                }
                if (j == pattern_len) {
                    found = 1;
                    break;
                }
            }

            if (found) {
                print(line);
                print("\n");
                match = 1;
            }

            line_pos = 0;
        } else {
            line[line_pos++] = c;
        }
    }

    close(fd);
    return match ? 0 : 1;
}
