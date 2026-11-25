#include "kyroolib.h"

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: chown [OWNER][:GROUP] FILE...\n");
        print("Change file owner and group.\n");
        print_flush();
        return 0;
    }
    if (argc < 3) {
        puts("Usage: chown <uid[:gid]> <file>\n");
        return 1;
    }

    uint32_t uid = 0;
    uint32_t gid = 0;

    char *colon = strchr(argv[1], ':');
    if (colon) {
        *colon = '\0';
        uid = atoi(argv[1]);
        gid = atoi(colon + 1);
    } else {
        uid = atoi(argv[1]);
        gid = uid;
    }

    if (chown(argv[2], uid, gid) != 0) {
        puts("chown: cannot change ownership of '");
        puts(argv[2]);
        puts("': Operation failed\n");
        return 1;
    }
    return 0;
}
