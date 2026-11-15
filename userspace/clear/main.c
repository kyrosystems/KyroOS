#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: clear\n");
        print("Clear the terminal screen.\n");
        print_flush();
        return 0;
    }
    (void)argc;
    (void)argv;

    // ANSI escape code to clear screen
    print("\033[2J\033[H");
    return 0;
}
