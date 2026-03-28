#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1) {
        // Set hostname (would need proper syscall)
        print("hostname: set to '");
        print(argv[1]);
        print("'\n");
    } else {
        // Get hostname
        print("localhost\n");
    }
    return 0;
}
