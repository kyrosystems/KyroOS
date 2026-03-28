#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        print("Usage: mount <device> <mountpoint> [fstype]\n");
        print("  device: device path (e.g., /dev/sda1)\n");
        print("  mountpoint: directory to mount on\n");
        print("  fstype: filesystem type (default: kyrofs)\n");
        return 1;
    }

    const char *device = argv[1];
    const char *mountpoint = argv[2];
    const char *fstype = (argc > 3) ? argv[3] : "kyrofs";

    // Use mount syscall
    int result = syscall(SYS_MOUNT, (uint64_t)mountpoint, (uint64_t)device, (uint64_t)fstype);

    if (result == 0) {
        print("mount: mounted '");
        print(device);
        print("' on '");
        print(mountpoint);
        print("' (");
        print(fstype);
        print(")\n");
        return 0;
    } else {
        print("mount: failed to mount '");
        print(device);
        print("'\n");
        return 1;
    }
}
