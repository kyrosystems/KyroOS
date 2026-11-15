#include "kyroolib.h"
#include "version.h"

static void to_hex(const uint8_t *h, char *out) {
    const char *hx = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2]     = hx[h[i] >> 4];
        out[i * 2 + 1] = hx[h[i] & 15];
    }
    out[64] = '\0';
}

static void hash_password(const char *salt_hex, const char *pw, char *out) {
    uint8_t salt[8], hash[32];
    for (int i = 0; i < 8; i++) {
        salt[i] = (uint8_t)((salt_hex[i * 2] - '0' <= 9 ? salt_hex[i * 2] - '0'
            : salt_hex[i * 2] - 'a' + 10) * 16
            + (salt_hex[i * 2 + 1] - '0' <= 9 ? salt_hex[i * 2 + 1] - '0'
            : salt_hex[i * 2 + 1] - 'a' + 10));
    }
    uint8_t data[128];
    memcpy(data, salt, 8);
    size_t pl = strlen(pw);
    if (pl > sizeof(data) - 8) pl = sizeof(data) - 8;
    memcpy(data + 8, pw, pl);
    sha256(data, 8 + pl, hash);
    to_hex(hash, out);
}

static void read_input(char *buf, int max, bool hide) {
    int idx = 0;
    while (idx < max - 1) {
        char ch = 0;
        int n = read(0, &ch, 1);
        if (n <= 0) continue;
        if (ch == '\r' || ch == '\n') {
            print("\n");
            print_flush();
            break;
        } else if (ch == '\b' || ch == 127) {
            if (idx > 0) {
                idx--;
                if (!hide) {
                    print("\b \b");
                    print_flush();
                }
            }
        } else if (ch >= 32 && ch <= 126) {
            buf[idx++] = ch;
            if (!hide) {
                char out[2] = {ch, '\0'};
                print(out);
                print_flush();
            }
        }
    }
    buf[idx] = '\0';
}

static void print_banner(void) {
    char hname[64] = "kyroos";
    gethostname(hname, sizeof(hname));
    print("\n\033[1;36m============================================================\033[0m\n");
    print("\033[1;32m  ");
    print(KYROOS_VERSION_STRING);
    print(" (build ");
    print(KYROOS_VERSION_BUILD);
    print(" x86_64) - ");
    print(hname);
    print("\033[0m\n");
    print("\033[1;36m============================================================\033[0m\n\n");
    print_flush();
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    // Check hostname from /etc/hostname
    int hfd = open("/etc/hostname", O_RDONLY);
    if (hfd >= 0) {
        char hbuf[64];
        int n = read(hfd, hbuf, sizeof(hbuf) - 1);
        if (n > 0) {
            hbuf[n] = '\0';
            for (int i = 0; i < n; i++) {
                if (hbuf[i] == '\r' || hbuf[i] == '\n' || hbuf[i] == ' ') {
                    hbuf[i] = '\0';
                    break;
                }
            }
            if (hbuf[0]) {
                sethostname(hbuf, strlen(hbuf));
            }
        }
        close(hfd);
    }

    print_banner();

    // Linux-style Login Loop
    while (1) {
        char user[64];
        char pass[64];
        char shadow_hash[128];
        char user_shell[64] = "/bin/sh";
        char user_home[128] = "/";
        uint32_t login_uid = 0;
        uint32_t login_gid = 0;
        bool user_found = false;

        print("kyroos login: ");
        print_flush();
        read_input(user, sizeof(user), false);
        if (user[0] == '\0') continue;

        // Lookup user in /etc/passwd
        int pfd = open("/etc/passwd", O_RDONLY);
        if (pfd >= 0) {
            char pbuf[4096];
            int n = read(pfd, pbuf, sizeof(pbuf) - 1);
            close(pfd);
            if (n > 0) {
                pbuf[n] = '\0';
                char *line = pbuf;
                while (line && *line) {
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = '\0';
                    char *cr = strchr(line, '\r');
                    if (cr) *cr = '\0';
                    char *fields[7];
                    fields[0] = line;
                    int count = 1;
                    for (char *p = line; *p && count < 7; p++) {
                        if (*p == ':') {
                            *p = '\0';
                            fields[count++] = p + 1;
                        }
                    }
                    if (count == 7 && strcmp(fields[0], user) == 0) {
                        user_found = true;
                        login_uid = (uint32_t)atoi(fields[2]);
                        login_gid = (uint32_t)atoi(fields[3]);
                        strncpy(user_home, fields[5], sizeof(user_home) - 1);
                        user_home[sizeof(user_home) - 1] = '\0';
                        if (fields[6][0]) {
                            strncpy(user_shell, fields[6], sizeof(user_shell) - 1);
                            user_shell[sizeof(user_shell) - 1] = '\0';
                        }
                        break;
                    }
                    line = nl ? nl + 1 : NULL;
                }
            }
        }

        shadow_hash[0] = '\0';
        int sfd = open("/etc/shadow", O_RDONLY);
        if (sfd >= 0) {
            char sbuf[4096];
            int n = read(sfd, sbuf, sizeof(sbuf) - 1);
            close(sfd);
            if (n > 0) {
                sbuf[n] = '\0';
                char *line = sbuf;
                while (line && *line) {
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = '\0';
                    char *f1 = strchr(line, ':');
                    if (f1) {
                        *f1 = '\0';
                        if (strcmp(line, user) == 0) {
                            char *f2 = strchr(f1 + 1, ':');
                            if (f2) *f2 = '\0';
                            strncpy(shadow_hash, f1 + 1, sizeof(shadow_hash) - 1);
                            break;
                        }
                    }
                    line = nl ? nl + 1 : NULL;
                }
            }
        }

        // If user not in /etc/passwd or fallback root
        if (!user_found) {
            if (strcmp(user, "root") == 0) {
                user_found = true;
                login_uid = 0;
                login_gid = 0;
            } else {
                print("Password: ");
                print_flush();
                read_input(pass, sizeof(pass), true);
                print("\033[1;31mLogin incorrect\033[0m\n\n");
                print_flush();
                sleep_ms(1000);
                continue;
            }
        }

        // Verify password if set
        bool pass_required = false;
        if (shadow_hash[0] && shadow_hash[0] != '*' && shadow_hash[0] != '!') {
            pass_required = true;
        }

        if (pass_required) {
            print("Password: ");
            print_flush();
            read_input(pass, sizeof(pass), true);

            char *dollar = strchr(shadow_hash, '$');
            if (dollar) {
                *dollar = '\0';
                const char *salt = shadow_hash;
                const char *expected_hash = dollar + 1;
                char computed_hash[65];
                hash_password(salt, pass, computed_hash);
                if (strcmp(computed_hash, expected_hash) != 0) {
                    print("\033[1;31mLogin incorrect\033[0m\n\n");
                    print_flush();
                    sleep_ms(1000);
                    continue;
                }
            }
        }

        // Run the session in a child so init retains credentials for later logins
        print("init: starting session ");
        print(user_shell);
        print("\n");
        print_flush();
        char uid_arg[16], gid_arg[16];
        sprintf(uid_arg, "%u", login_uid);
        sprintf(gid_arg, "%u", login_gid);
        char *session_argv[] = {"/bin/session", uid_arg, gid_arg, user_home, user_shell, NULL};
        int pid = execve(session_argv[0], session_argv, NULL);

        if (pid < 0) {
            print("\033[1;31minit: cannot execute shell!\033[0m\n");
            print_flush();
            continue;
        }

        int status = 0;
        waitpid(pid, &status, 0);

        // The session child has exited; init never dropped its credentials
        print("\n");
        print_banner();
    }

    return 0;
}
