#include "kyroolib.h"

static uint32_t parse_octal(const char *s) {
    uint32_t val = 0;
    while (*s >= '0' && *s <= '7') {
        val = (val << 3) | (*s - '0');
        s++;
    }
    return val;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: chmod MODE FILE...\n");
        print("Change file mode bits (e.g. 0755, 0644).\n");
        print_flush();
        return 0;
    }
    if (argc < 3) {
        puts("Usage: chmod <octal_mode> <file>\n");
        return 1;
    }

    uint32_t mode = parse_octal(argv[1]);
    if (chmod(argv[2], mode) != 0) {
        puts("chmod: cannot change mode of '");
        puts(argv[2]);
        puts("': Operation failed\n");
        return 1;
    }
    return 0;
}
