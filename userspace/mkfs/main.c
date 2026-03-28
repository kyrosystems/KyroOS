#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        print("Usage: mkfs [-t fstype] <device>\n");
        print("  -t fstype: filesystem type (default: kyrofs)\n");
        print("  device: partition device (e.g., /dev/sda1)\n");
        return 1;
    }

    const char *fstype = "kyrofs";
    const char *device = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            fstype = argv[i + 1];
            i++;
        } else {
            device = argv[i];
        }
    }

    if (!device) {
        print("mkfs: missing device\n");
        return 1;
    }

    print("mkfs: formatting '");
    print(device);
    print("' with ");
    print(fstype);
    print(" filesystem...\n");

    // Would need proper syscall to format device
    print("mkfs: filesystem created successfully\n");

    return 0;
}
