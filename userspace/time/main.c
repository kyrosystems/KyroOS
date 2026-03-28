#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: time <command>\n");
        return 1;
    }

    uint64_t start = syscall(SYS_GET_TICKS, 0, 0, 0);

    print("Executing: ");
    for (int i = 1; i < argc; i++) {
        print(argv[i]);
        print(" ");
    }
    print("\n");

    // Would need proper exec

    uint64_t end = syscall(SYS_GET_TICKS, 0, 0, 0);
    uint64_t elapsed = end - start;

    print("\nReal: ");
    print_u32(elapsed / 100);
    print(".");
    print_u32(elapsed % 100);
    print("s\n");

    return 0;
}
