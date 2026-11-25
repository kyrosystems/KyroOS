#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: cat [FILE]...\n");
        print("Concatenate FILE(s) to standard output.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        print("Usage: cat <file>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            print("cat: cannot open file\n");
            continue;
        }

        char buf[1024];
        int n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(1, buf, n);
        }
        close(fd);
    }
    return 0;
}
