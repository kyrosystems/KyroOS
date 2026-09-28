#include <kyroolib.h>
#include <errno.h>

/*
 * id - print real/effective UID/GID of the current process.
 * Group supplementary list is not yet supported by the kernel credential
 * model (documented limitation); only the primary gid is reported.
 */
static void print_u(const char *label, uint32_t v) {
    char b[64];
    sprintf(b, "%s=%u", label, v);
    print(b);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint32_t uid = getuid(), euid = geteuid();
    uint32_t gid = getgid(), egid = getegid();

    print_u("uid", uid);
    print_u(" gid", gid);
    if (euid != uid) { print_u(" euid", euid); }
    if (egid != gid) { print_u(" egid", egid); }
    print("\n");
    return 0;
}