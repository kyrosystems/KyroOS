#include <kyroolib.h>

/*
 * whoami - print the user name of the effective UID.
 * Looks up /etc/passwd (name:x:uid:gid:gecos:home:shell). Falls back to
 * "uid <N>" when no user database exists (live ISO before installer setup).
 */
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

int main(void) {
    uint32_t euid = geteuid();

    int fd = open("/etc/passwd", O_RDONLY);
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
                char *fields[7];
                int nf = 0;
                char *p = trim(line);
                while (nf < 7 && p && *p) {
                    fields[nf++] = p;
                    char *c = strchr(p, ':');
                    if (!c) break;
                    *c = '\0';
                    p = trim(c + 1);
                }
                if (nf >= 3) {
                    uint32_t uid = 0;
                    for (const char *q = fields[2]; *q; q++)
                        if (*q >= '0' && *q <= '9') uid = uid * 10 + (uint32_t)(*q - '0');
                    if (uid == euid) {
                        print(fields[0]);
                        print("\n");
                        return 0;
                    }
                }
                line = nl ? nl + 1 : NULL;
            }
        }
    }

    /* No user database: report the numeric effective uid */
    print("uid ");
    print_u32(euid);
    print("\n");
    return 0;
}