#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: lsmod\n");
        print("Show the status of modules in the kernel.\n");
        print_flush();
        return 0;
    }
    (void)argc; (void)argv;
    int fd = open("/dev/lkm", O_RDONLY);
    if (fd < 0) {
        puts("lsmod: cannot open /dev/lkm (LKM support not active)\n");
        return 1;
    }

    char buf[512];
    int n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        print(buf);
    }
    print_flush();
    close(fd);
    return 0;
}
