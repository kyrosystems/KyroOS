#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: su [USER]\n");
        print("Change user ID or become superuser.\n");
        print_flush();
        return 0;
    }
    const char *target_user = (argc > 1) ? argv[1] : "root";
    uint32_t target_uid = 0;
    uint32_t target_gid = 0;

    if (strcmp(target_user, "root") != 0) {
        // Look up in /etc/passwd
        int fd = open("/etc/passwd", O_RDONLY);
        if (fd < 0) {
            puts("su: cannot open /etc/passwd\n");
            return 1;
        }
        char buf[2048];
        int n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) {
            puts("su: user not found\n");
            return 1;
        }
        buf[n] = '\0';
        char *line = buf;
        int found = 0;
        int tlen = strlen(target_user);
        while (line && *line) {
            char *next = strchr(line, '\n');
            if (next) *next++ = '\0';
            if (strncmp(line, target_user, tlen) == 0 && line[tlen] == ':') {
                char *c1 = strchr(line + tlen + 1, ':');
                if (c1) {
                    char *c2 = strchr(c1 + 1, ':');
                    if (c2) {
                        target_uid = atoi(c1 + 1);
                        target_gid = atoi(c2 + 1);
                        found = 1;
                        break;
                    }
                }
            }
            line = next;
        }
        if (!found) {
            printf("su: user '%s' does not exist\n", target_user);
            print_flush();
            return 1;
        }
    }

    if (getuid() != 0) {
        puts("Password: ");
        // (In non-root mode, verify against /etc/shadow or proceed if empty)
    }

    setgid(target_gid);
    setuid(target_uid);

    char *sh_argv[] = {"/bin/sh", NULL};
    int pid = execve("/bin/sh", sh_argv, NULL);
    if (pid >= 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        return status;
    }

    puts("su: failed to execute /bin/sh\n");
    return 1;
}