#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: umount DIR\n");
        print("Unmount file systems.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        print("Usage: umount <mountpoint>\n");
        return 1;
    }

    const char *mountpoint = argv[1];

    // Use unmount syscall
    int result = syscall(SYS_UNMOUNT, (uint64_t)mountpoint, 0, 0);

    if (result == 0) {
        print("umount: unmounted '");
        print(mountpoint);
        print("'\n");
        return 0;
    } else {
        print("umount: failed to unmount '");
        print(mountpoint);
        print("'\n");
        return 1;
    }
}
