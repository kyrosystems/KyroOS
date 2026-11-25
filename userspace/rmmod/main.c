#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: rmmod MODULE_NAME\n");
        print("Unload a kernel module.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        puts("Usage: rmmod <module_name>\n");
        return 1;
    }

    int fd = open("/dev/lkm", O_WRONLY);
    if (fd < 0) {
        puts("rmmod: cannot open /dev/lkm\n");
        return 1;
    }

    char cmd[128];
    cmd[0] = '-';
    strncpy(cmd + 1, argv[1], sizeof(cmd) - 2);
    cmd[sizeof(cmd) - 1] = '\0';

    int len = strlen(cmd);
    int written = write(fd, cmd, len);
    close(fd);

    if (written < len) {
        puts("rmmod: failed to unload module '");
        puts(argv[1]);
        puts("'\n");
        return 1;
    }

    puts("rmmod: module '");
    puts(argv[1]);
    puts("' unloaded\n");
    return 0;
}
