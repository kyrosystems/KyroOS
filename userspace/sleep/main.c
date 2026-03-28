#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: sleep <seconds>\n");
        return 1;
    }

    // Parse seconds
    uint32_t seconds = 0;
    const char *s = argv[1];
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            seconds = seconds * 10 + (s[i] - '0');
        }
    }

    // Simple busy-wait delay (would need proper sleep syscall)
    print("sleep: sleeping for ");
    print_u32(seconds);
    print(" seconds\n");

    // Approximate delay (assuming ~100 ticks per second)
    uint64_t start = syscall(SYS_GET_TICKS, 0, 0, 0);
    uint64_t target = start + (seconds * 100);

    while (syscall(SYS_GET_TICKS, 0, 0, 0) < target) {
        // Busy wait
    }

    return 0;
}
