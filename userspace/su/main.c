#include <kyroolib.h>
#include <errno.h>
#include <string.h>

/*
 * su - switch effective credentials to another user (root only).
 *
 * KyroOS model limitation (documented in abi/syscalls.def): there is no
 * fork()/saved-set-uid yet, so non-root users cannot elevate. Root may
 * authenticate as any user and drop privileges with su.
 *
 *   su <user>   verify <user>'s password, then setuid/setgid to it
 */
static int read_line(const char *prompt, char *buf, int max) {
    if (prompt) print(prompt);
    int n = 0;
    for (;;) {
        char c;
        int r = read(0, &c, 1);
        if (r <= 0) break;
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 127) {
            if (n > 0) n--;
            continue;
        }
        if (c >= 32 && c < 127 && n < max - 1) buf[n++] = c;
    }
    buf[n] = '\0';
    print("\n");
    return n;
}

static void to_hex(const uint8_t *h, char *out) {
    const char *hx = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2]     = hx[h[i] >> 4];
        out[i * 2 + 1] = hx[h[i] & 15];
    }
    out[64] = '\0';
}

static int check_password(const char *user, const char *pw) {
    int fd = open("/etc/shadow", O_RDONLY);
    if (fd < 0) return -1;
    char buf[8192];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *c1 = strchr(line, ':');
        if (c1) {
            *c1 = '\0';
            if (strcmp(line, user) == 0) {
                const char *stored = c1 + 1;
                /* stored = "salt_hex(16)$hash_hex(64)" */
                if (strlen(stored) != 16 + 1 + 64) return -1;
                char salt[17];
                memcpy(salt, stored, 16);
                salt[16] = '\0';
                uint8_t saltb[8];
                for (int i = 0; i < 8; i++) {
                    char hi = salt[i * 2], lo = salt[i * 2 + 1];
                    saltb[i] = (uint8_t)((hi <= '9' ? hi - '0' : hi - 'a' + 10) * 16
                                       + (lo <= '9' ? lo - '0' : lo - 'a' + 10));
                }
                uint8_t data[128];
                memcpy(data, saltb, 8);
                size_t pl = strlen(pw);
                if (pl > sizeof(data) - 8) pl = sizeof(data) - 8;
                memcpy(data + 8, pw, pl);
                uint8_t hh[32];
                sha256(data, 8 + pl, hh);
                char hex[65];
                to_hex(hh, hex);
                return strcmp(hex, stored + 17) == 0 ? 0 : -1;
            }
            *c1 = ':';
        }
        line = nl ? nl + 1 : NULL;
    }
    return -1;
}

static uint32_t lookup_uid(const char *user) {
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) return 0xFFFFFFFF;
    char buf[8192];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0xFFFFFFFF;
    buf[n] = '\0';
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *f1 = strchr(line, ':');
        if (f1) {
            char *f2 = strchr(f1 + 1, ':');
            if (f2) {
                char *f3 = strchr(f2 + 1, ':');
                if (f3) {
                    if ((int)(f1 - line) == (int)strlen(user) &&
                        strncmp(line, user, (size_t)(f1 - line)) == 0) {
                        uint32_t uid = 0;
                        for (const char *q = f2 + 1; *q && *q != ':'; q++)
                            if (*q >= '0' && *q <= '9') uid = uid * 10 + (uint32_t)(*q - '0');
                        return uid;
                    }
                }
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    return 0xFFFFFFFF;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: su <user>\n");
        return 1;
    }
    if (geteuid() != 0) {
        print("su: only root may switch users (no saved-set-uid model yet)\n");
        return 1;
    }

    uint32_t uid = lookup_uid(argv[1]);
    if (uid == 0xFFFFFFFF) {
        print("su: unknown user ");
        print(argv[1]);
        print("\n");
        return 1;
    }

    char pw[64];
    read_line("Password: ", pw, sizeof(pw));
    if (check_password(argv[1], pw) != 0) {
        print("su: authentication failed\n");
        return 1;
    }

    if (setgid(uid) != 0 || setuid(uid) != 0) {
        print("su: setuid failed (errno ");
        print_u32((uint32_t)errno);
        print(")\n");
        return 1;
    }

    print("su: credentials switched to ");
    print(argv[1]);
    print("\n");
    return 0;
}