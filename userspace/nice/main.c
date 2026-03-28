#include <kyroolib.h>

int main(int argc, char **argv) {
    int priority = 10; // Default nice value
    const char *command = NULL;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            const char *n = argv[i + 1];
            priority = 0;
            for (int j = 0; n[j] != '\0'; j++) {
                if (n[j] >= '0' && n[j] <= '9') {
                    priority = priority * 10 + (n[j] - '0');
                }
            }
            i++;
        } else {
            command = argv[i];
        }
    }

    if (!command) {
        print("Usage: nice [-n priority] <command>\n");
        return 1;
    }

    print("nice: running '");
    print(command);
    print("' with priority ");
    print_u32(priority);
    print("\n");

    // Would need proper exec with priority
    return 0;
}
