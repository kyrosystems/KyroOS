#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: useradd [OPTIONS] USERNAME\n");
        print("Create a new user.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        puts("Usage: useradd <username>\n");
        return 1;
    }

    const char *username = argv[1];
    uint32_t next_uid = 1000;

    int fd = open("/etc/passwd", O_RDONLY);
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
                    if (strcmp(line, username) == 0) {
                        puts("useradd: user '");
                        puts(username);
                        puts("' already exists!\n");
                        return 1;
                    }
                    char *c2 = strchr(c1 + 1, ':');
                    if (c2) {
                        char *c3 = strchr(c2 + 1, ':');
                        if (c3) {
                            int uid = atoi(c2 + 1);
                            if (uid >= (int)next_uid) {
                                next_uid = uid + 1;
                            }
                        }
                    }
                }
                line = next;
            }
        }
    }

    // Append to /etc/passwd
    int pfd = open("/etc/passwd", O_WRONLY | O_CREAT | O_APPEND);
    if (pfd < 0) {
        puts("useradd: cannot write to /etc/passwd\n");
        return 1;
    }

    char pentry[256];
    snprintf(pentry, sizeof(pentry), "%s:x:%u:%u:%s:/home/%s:/bin/sh\n",
             username, next_uid, next_uid, username, username);
    write(pfd, pentry, strlen(pentry));
    close(pfd);

    // Append to /etc/shadow
    int sfd = open("/etc/shadow", O_WRONLY | O_CREAT | O_APPEND);
    if (sfd >= 0) {
        char sentry[256];
        snprintf(sentry, sizeof(sentry), "%s:*:19000:0:99999:7:::\n", username);
        write(sfd, sentry, strlen(sentry));
        close(sfd);
    }

    // Create home directory
    mkdir("/home");
    char homedir[128];
    snprintf(homedir, sizeof(homedir), "/home/%s", username);
    mkdir(homedir);
    chown(homedir, next_uid, next_uid);

    printf("useradd: user '%s' created (UID %u, GID %u, home: %s)\n",
           username, next_uid, next_uid, homedir);
    print_flush();
    return 0;
}
