#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        print("Usage: renice <priority> <pid>\n");
        return 1;
    }

    // Parse priority
    int priority = 0;
    const char *p = argv[1];
    for (int i = 0; p[i] != '\0'; i++) {
        if (p[i] >= '0' && p[i] <= '9') {
            priority = priority * 10 + (p[i] - '0');
        }
    }

    // Parse PID
    int pid = 0;
    const char *s = argv[2];
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            pid = pid * 10 + (s[i] - '0');
        }
    }

    print("renice: set priority ");
    print_u32(priority);
    print(" for process ");
    print_u32(pid);
    print("\n");

    // Would need proper syscall
    return 0;
}
