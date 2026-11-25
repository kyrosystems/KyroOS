#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: groupdel GROUP\n");
        print("Delete an existing user group.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        puts("Usage: groupdel <groupname>\n");
        return 1;
    }

    if (strcmp(argv[1], "root") == 0) {
        puts("groupdel: cannot delete root group!\n");
        return 1;
    }

    int fd = open("/etc/group", O_RDONLY);
    if (fd < 0) return 1;

    char buf[4096];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 1;

    buf[n] = '\0';
    char out[4096];
    int out_len = 0;
    int plen = strlen(argv[1]);

    char *line = buf;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';

        if (strncmp(line, argv[1], plen) == 0 && line[plen] == ':') {
            // skip
        } else if (*line) {
            int llen = strlen(line);
            if (out_len + llen + 2 < (int)sizeof(out)) {
                memcpy(out + out_len, line, llen);
                out_len += llen;
                out[out_len++] = '\n';
            }
        }
        line = next;
    }
    out[out_len] = '\0';

    int wfd = open("/etc/group", O_WRONLY | O_CREAT | O_TRUNC);
    if (wfd >= 0) {
        write(wfd, out, out_len);
        close(wfd);
    }

    printf("groupdel: group '%s' removed\n", argv[1]);
    print_flush();
    return 0;
}
