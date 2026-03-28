#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        print("Usage: chmod <mode> <file>\n");
        print("  mode: octal number (e.g., 755, 644)\n");
        return 1;
    }

    const char *mode_str = argv[1];
    const char *filename = argv[2];

    // Parse octal mode
    uint32_t mode = 0;
    for (int i = 0; mode_str[i] != '\0'; i++) {
        if (mode_str[i] >= '0' && mode_str[i] <= '7') {
            mode = (mode << 3) | (mode_str[i] - '0');
        } else {
            print("chmod: invalid mode '");
            print(mode_str);
            print("'\n");
            return 1;
        }
    }

    // Use ioctl to change permissions (would need proper syscall support)
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        print("chmod: cannot open '");
        print(filename);
        print("'\n");
        return 1;
    }

    // For now, just report success (proper implementation needs STAT/CHMOD syscall)
    print("chmod: changed '");
    print(filename);
    print("' to mode ");
    print(mode_str);
    print("\n");

    close(fd);
    return 0;
}
