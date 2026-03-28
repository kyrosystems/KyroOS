#include <kyroolib.h>

int main(int argc, char **argv) {
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
