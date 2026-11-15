#include <kyroolib.h>

/*
 * users - list users currently logged in. KyroOS currently has a single
 * login session (the boot shell), so this reports the effective user of
 * the system session, resolved via /etc/passwd.
 */
int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: users\n");
        print("Print the user names of users currently logged in to the system.\n");
        print_flush();
        return 0;
    }

    int fd = open("/etc/passwd", O_RDONLY);
    int printed = 0;
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
                    *f1 = '\0';
                    if (line[0] != '#' && line[0] != '\0') {
                        if (printed > 0) print(" ");
                        print(line);
                        printed++;
                    }
                }
                line = nl ? nl + 1 : NULL;
            }
        }
    }

    if (printed > 0) {
        print("\n");
    } else {
        uint32_t euid = geteuid();
        if (euid == 0) print("root\n");
        else {
            char b[32];
            sprintf(b, "user%u\n", euid);
            print(b);
        }
    }
    print_flush();
    return 0;
}