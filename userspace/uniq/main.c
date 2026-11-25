#include <kyroolib.h>

#define MAX_LINE 1024

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: uniq [INPUT [OUTPUT]]\n");
        print("Report or omit repeated lines.\n");
        print_flush();
        return 0;
    }
    (void)argc;
    (void)argv;

    char prev_line[MAX_LINE] = {0};
    char curr_line[MAX_LINE];
    int pos = 0;
    char c;

    while (read(0, &c, 1) > 0) {  // Read from stdin
        if (c == '\n' || pos >= MAX_LINE - 1) {
            if (c == '\n') {
                curr_line[pos] = '\0';
            } else {
                curr_line[pos] = c;
                pos++;
                continue;
            }

            // Only print if different from previous
            if (strcmp(curr_line, prev_line) != 0) {
                print(curr_line);
                print("\n");
                strcpy(prev_line, curr_line);
            }
            pos = 0;
        } else {
            curr_line[pos++] = c;
        }
    }

    return 0;
}
