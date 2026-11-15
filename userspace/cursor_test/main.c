#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: cursor_test\n");
        print("Test hardware/software cursor.\n");
        print_flush();
        return 0;
    }
    print("Hello from cursor_test!\n");
    return 0;
}