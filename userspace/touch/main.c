#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: touch <file>\n");
        return 1;
    }

    int fd = open(argv[1], O_CREAT | O_WRONLY);
    if (fd < 0) {
        print("touch: failed to create/open file\n");
        return 1;
    }
    close(fd);
    return 0;
}
