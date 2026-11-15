#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: id [USER]\n");
        print("Print user and group IDs.\n");
        print_flush();
        return 0;
    }
    (void)argc; (void)argv;
    uint32_t uid = getuid();
    uint32_t euid = geteuid();
    uint32_t gid = getgid();
    uint32_t egid = getegid();

    const char *uname = (uid == 0) ? "root" : "user";
    const char *gname = (gid == 0) ? "root" : "users";

    printf("uid=%u(%s) gid=%u(%s) euid=%u egid=%u\n", uid, uname, gid, gname, euid, egid);
    print_flush();
    return 0;
}