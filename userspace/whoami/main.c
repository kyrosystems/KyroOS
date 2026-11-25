#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: whoami\n");
        print("Print effective user ID.\n");
        print_flush();
        return 0;
    }
    (void)argc; (void)argv;
    uint32_t uid = getuid();
    if (uid == 0) {
        puts("root\n");
        return 0;
    }

    int fd = open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        char buf[1024];
        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *line = buf;
            while (line && *line) {
                char *next = strchr(line, '\n');
                if (next) *next++ = '\0';
                char uname[32];
                int u_uid = -1;
                // parse name:x:uid:
                char *p1 = strchr(line, ':');
                if (p1) {
                    int unamelen = p1 - line;
                    if (unamelen < (int)sizeof(uname)) {
                        strncpy(uname, line, unamelen);
                        uname[unamelen] = '\0';
                        char *p2 = strchr(p1 + 1, ':');
                        if (p2) {
                            u_uid = atoi(p2 + 1);
                            if ((uint32_t)u_uid == uid) {
                                puts(uname);
                                puts("\n");
                                return 0;
                            }
                        }
                    }
                }
                line = next;
            }
        }
    }

    printf("uid-%u\n", uid);
    print_flush();
    return 0;
}