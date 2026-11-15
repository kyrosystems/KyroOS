#include "kyroolib.h"

static void filter_file(const char *path, const char *prefix) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;

    char buf[4096];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;

    buf[n] = '\0';
    char out[4096];
    int out_len = 0;
    int plen = strlen(prefix);

    char *line = buf;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';

        if (strncmp(line, prefix, plen) == 0 && line[plen] == ':') {
            // Skip this line
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

    int wfd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (wfd >= 0) {
        write(wfd, out, out_len);
        close(wfd);
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: userdel USERNAME\n");
        print("Delete a user account and related files.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        puts("Usage: userdel <username>\n");
        return 1;
    }

    if (strcmp(argv[1], "root") == 0) {
        puts("userdel: cannot delete root user!\n");
        return 1;
    }

    filter_file("/etc/passwd", argv[1]);
    filter_file("/etc/shadow", argv[1]);

    printf("userdel: user '%s' removed\n", argv[1]);
    print_flush();
    return 0;
}
