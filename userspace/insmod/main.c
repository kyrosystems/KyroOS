#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: insmod MODULE.ko\n");
        print("Insert a loadable kernel module.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        puts("Usage: insmod <path_to_module.ko>\n");
        return 1;
    }

    int fd = open("/dev/lkm", O_WRONLY);
    if (fd < 0) {
        puts("insmod: cannot open /dev/lkm\n");
        return 1;
    }

    int len = strlen(argv[1]);
    int written = write(fd, argv[1], len);
    close(fd);

    if (written < len) {
        puts("insmod: failed to load module '");
        puts(argv[1]);
        puts("'\n");
        return 1;
    }

    puts("insmod: module '");
    puts(argv[1]);
    puts("' loaded successfully\n");
    return 0;
}
