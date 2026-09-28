#include <kyroolib.h>

/*
 * groups - print primary gid of the effective credentials, resolved to a
 * group name via /etc/group (name:x:gid:) when available.
 */
int main(void) {
    uint32_t egid = getegid();

    int fd = open("/etc/group", O_RDONLY);
    if (fd >= 0) {
        char buf[4096];
        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *line = buf;
            while (line && *line) {
                char *nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                char *f1 = strchr(line, ':');        /* end of group name */
                if (f1) {
                    char *f2 = strchr(f1 + 1, ':');  /* end of password */
                    if (f2) {
                        uint32_t gid = 0;
                        for (const char *q = f2 + 1; *q && *q != ':'; q++)
                            if (*q >= '0' && *q <= '9') gid = gid * 10 + (uint32_t)(*q - '0');
                        if (gid == egid) {
                            /* print group name */
                            *f1 = '\0';
                            print(line);
                            print("\n");
                            return 0;
                        }
                    }
                }
                line = nl ? nl + 1 : NULL;
            }
        }
    }

    char b[32];
    sprintf(b, "%u\n", egid);
    print(b);
    return 0;
}