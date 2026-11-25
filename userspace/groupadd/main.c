#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: groupadd GROUP\n");
        print("Create a new user group.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        puts("Usage: groupadd <groupname>\n");
        return 1;
    }

    const char *groupname = argv[1];
    uint32_t next_gid = 1000;

    int fd = open("/etc/group", O_RDONLY);
    if (fd >= 0) {
        char buf[2048];
        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *line = buf;
            while (line && *line) {
                char *next = strchr(line, '\n');
                if (next) *next++ = '\0';

                char *c1 = strchr(line, ':');
                if (c1) {
                    *c1 = '\0';
                    if (strcmp(line, groupname) == 0) {
                        puts("groupadd: group '");
                        puts(groupname);
                        puts("' already exists!\n");
                        return 1;
                    }
                    char *c2 = strchr(c1 + 1, ':');
                    if (c2) {
                        int gid = atoi(c2 + 1);
                        if (gid >= (int)next_gid) {
                            next_gid = gid + 1;
                        }
                    }
                }
                line = next;
            }
        }
    }

    int gfd = open("/etc/group", O_WRONLY | O_CREAT | O_APPEND);
    if (gfd < 0) {
        puts("groupadd: cannot write to /etc/group\n");
        return 1;
    }

    char gentry[128];
    snprintf(gentry, sizeof(gentry), "%s:x:%u:\n", groupname, next_gid);
    write(gfd, gentry, strlen(gentry));
    close(gfd);

    printf("groupadd: group '%s' created (GID %u)\n", groupname, next_gid);
    print_flush();
    return 0;
}
