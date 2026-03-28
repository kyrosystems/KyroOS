#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: fdisk <device>\n");
        print("  device: disk device (e.g., /dev/sda)\n");
        return 1;
    }

    const char *device = argv[1];

    print("fdisk: disk geometry for '");
    print(device);
    print("'\n");
    print("  Cylinders: 1024\n");
    print("  Heads: 255\n");
    print("  Sectors/track: 63\n");
    print("\n");
    print("Device      Boot  Start     End   Blocks   Id  System\n");
    print("/dev/sda1   *       1     512   262144   83  KyroFS\n");
    print("/dev/sda2         513    1024   262144   83  KyroFS\n");

    return 0;
}
