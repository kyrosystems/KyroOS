#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: hostname [NEW_NAME]\n");
        print("Show or set system host name.\n");
        print_flush();
        return 0;
    }
    if (argc > 1) {
        if (sethostname(argv[1], strlen(argv[1])) != 0) {
            puts("hostname: cannot set hostname (permission denied or name too long)\n");
            return 1;
        }
        // Also persist to /etc/hostname
        int fd = open("/etc/hostname", O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            write(fd, argv[1], strlen(argv[1]));
            write(fd, "\n", 1);
            close(fd);
        }
        return 0;
    }

    char hname[64];
    if (gethostname(hname, sizeof(hname)) == 0) {
        puts(hname);
        puts("\n");
    } else {
        puts("kyroos\n");
    }
    return 0;
}
