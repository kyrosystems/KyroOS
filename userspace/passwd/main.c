#include <kyroolib.h>
#include <errno.h>
#include <string.h>

/*
 * passwd - change user passwords.
 *   passwd            change own password (verifies current one first)
 *   passwd <user>     change <user>'s password (root only)
 *
 * Storage: /etc/shadow lines "user:salt$hexhash", salt read from
 * /dev/urandom; hash = SHA-256(salt || password) via the SYS_SHA256
 * syscall. Plaintext passwords are never stored.
 * /etc/passwd keeps "user:x:uid:gid:gecos:home:shell" metadata.
 */

#define SALT_LEN 8

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

static void hash_password(const char *salt_hex, const char *pw, char *out) {
    /* salt stored as hex in the file: hash = SHA256(salt_bytes || pw) */
    uint8_t salt[SALT_LEN], hash[32];
    for (int i = 0; i < SALT_LEN; i++) {
        salt[i] = (uint8_t)((salt_hex[i * 2] - '0' <= 9 ? salt_hex[i * 2] - '0'
            : salt_hex[i * 2] - 'a' + 10) * 16
            + (salt_hex[i * 2 + 1] - '0' <= 9 ? salt_hex[i * 2 + 1] - '0'
            : salt_hex[i * 2 + 1] - 'a' + 10));
    }
    uint8_t data[128];
    memcpy(data, salt, SALT_LEN);
    size_t pl = strlen(pw);
    if (pl > sizeof(data) - SALT_LEN) pl = sizeof(data) - SALT_LEN;
    memcpy(data + SALT_LEN, pw, pl);
    sha256(data, SALT_LEN + pl, hash);
    to_hex(hash, out);
}

static int read_random_salt(char *out) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -1;
    uint8_t salt[SALT_LEN];
    int n = read(fd, salt, SALT_LEN);
    close(fd);
    if (n != SALT_LEN) return -1;
    to_hex(salt, out);
    return 0;
}

static int get_entry(const char *file, const char *user, char *out, int max) {
    int fd = open(file, O_RDONLY);
    if (fd < 0) return 0;
    char buf[8192];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *c1 = strchr(line, ':');
        if (c1) {
            *c1 = '\0';
            if (strcmp(line, user) == 0) {
                strncpy(out, c1 + 1, max - 1);
                out[max - 1] = '\0';
                return 1;
            }
            *c1 = ':';
        }
        line = nl ? nl + 1 : NULL;
    }
    return 0;
}

static int upsert_entry(const char *file, const char *user, const char *value) {
    /* read all, replace user's line or append */
    char buf[8192];
    int total = 0;
    int fd = open(file, O_RDONLY);
    int exists = 0;
    if (fd >= 0) {
        total = read(fd, buf, sizeof(buf) - 1);
        if (total < 0) total = 0;
        close(fd);
        buf[total] = '\0';
    }
    char out[8192];
    int o = 0;
    char *line = buf;
    while (line && *line && total > 0) {
        char *nl = strchr(line, '\n');
        int len = nl ? (int)(nl - line) : (int)strlen(line);
        char c1;
        (void)c1;
        char *c = strchr(line, ':');
        if (c && (int)(c - line) < len) {
            int ulen = (int)(c - line);
            if (ulen == (int)strlen(user) && strncmp(line, user, ulen) == 0) {
                o += sprintf(out + o, "%s:%s\n", user, value);
                exists = 1;
            } else {
                memcpy(out + o, line, len);
                o += len;
                out[o++] = '\n';
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    if (!exists) o += sprintf(out + o, "%s:%s\n", user, value);
    fd = open(file, O_WRONLY | O_CREAT);
    if (fd < 0) return -1;
    int w = write(fd, out, o);
    close(fd);
    return (w == o) ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: passwd [USER]\n");
        print("Change user password.\n");
        print_flush();
        return 0;
    }
    uint32_t euid = geteuid();
    const char *target = NULL;

    if (argc > 1) {
        target = argv[1];
        if (euid != 0) {
            print("passwd: only root may change other users' passwords\n");
            return 1;
        }
    } else {
        /* own password: resolve name via /etc/passwd */
        char ent[512];
        char uidstr[16];
        sprintf(uidstr, "%u", geteuid());
        (void)ent;
        target = NULL;
        /* find name by uid */
        int fd = open("/etc/passwd", O_RDONLY);
        if (fd >= 0) {
            char buf[8192];
            int n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            buf[n > 0 ? n : 0] = '\0';
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
                        if (uid == geteuid()) {
                            *f1 = '\0';
                            target = line;
                            break;
                        }
                    }
                }
                line = nl ? nl + 1 : NULL;
            }
        }
        if (!target) {
            print("passwd: no user database entry for current uid; use `passwd <name>` as root\n");
            return 1;
        }
        /* non-root must verify current password first */
        if (euid != 0) {
            char stored[128], cur[64], old[64];
            if (!get_entry("/etc/shadow", target, stored, sizeof(stored))) {
                print("passwd: no password set for this user; root must initialize it\n");
                return 1;
            }
            read_line("Current password: ", old, sizeof(old));
            char salt[17];
            memcpy(salt, stored, 16);
            salt[16] = '\0';
            char hh[65];
            hash_password(salt, old, hh);
            if (strcmp(hh, stored + 17) != 0) {
                print("passwd: incorrect current password\n");
                return 1;
            }
            (void)cur;
        }
    }

    char pw1[64], pw2[64];
    read_line("New password: ", pw1, sizeof(pw1));
    read_line("Retype new password: ", pw2, sizeof(pw2));
    if (strcmp(pw1, pw2) != 0) {
        print("passwd: passwords do not match\n");
        return 1;
    }
    if (strlen(pw1) < 4) {
        print("passwd: password too short (min 4 chars)\n");
        return 1;
    }

    char salt[17];
    if (read_random_salt(salt) != 0) {
        print("passwd: cannot read /dev/urandom for salt\n");
        return 1;
    }
    char hh[65];
    hash_password(salt, pw1, hh);
    char entry[96];
    sprintf(entry, "%s$%s", salt, hh);

    if (upsert_entry("/etc/shadow", target, entry) != 0) {
        print("passwd: failed to write /etc/shadow\n");
        return 1;
    }
    /* ensure /etc/passwd metadata entry exists */
    char pwent[512];
    if (!get_entry("/etc/passwd", target, pwent, sizeof(pwent))) {
        char meta[128];
        sprintf(meta, "x:0:0:%s:/home/%s:/bin/shell", target, target);
        upsert_entry("/etc/passwd", target, meta);
    }

    print("passwd: password updated for ");
    print(target);
    print("\n");
    return 0;
}