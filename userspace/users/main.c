#include <kyroolib.h>

/*
 * users - list users currently logged in. KyroOS currently has a single
 * login session (the boot shell), so this reports the effective user of
 * the system session, resolved via /etc/passwd.
 */
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
                char *f1 = strchr(line, ':');
                if (f1) {
                    char *f2 = strchr(f1 + 1, ':');
                    if (f2) {
                        uint32_t uid = 0;
                        for (const char *q = f2 + 1; *q && *q != ':'; q++)
                            if (*q >= '0' && *q <= '9') uid = uid * 10 + (uint32_t)(*q - '0');
                        if (uid == euid) {
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
    sprintf(b, "uid %u\n", euid);
    print(b);
    return 0;
}