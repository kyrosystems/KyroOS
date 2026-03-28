#include <kyroolib.h>

#define MAX_LINE 1024

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: wc <file>\n");
        return 1;
    }

    const char *filename = argv[1];
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        print("wc: cannot open '");
        print(filename);
        print("'\n");
        return 1;
    }

    int lines = 0;
    int words = 0;
    int bytes = 0;
    int in_word = 0;

    char c;
    while (read(fd, &c, 1) > 0) {
        bytes++;

        if (c == '\n') {
            lines++;
        }

        if (c == ' ' || c == '\n' || c == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }

    close(fd);

    print_u32(lines);
    print(" ");
    print_u32(words);
    print(" ");
    print_u32(bytes);
    print(" ");
    print(filename);
    print("\n");

    return 0;
}
