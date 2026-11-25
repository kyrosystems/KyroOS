#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: reset\n");
        print("Reset the terminal emulator.\n");
        print_flush();
        return 0;
    }
    (void)argc;
    (void)argv;

    // ANSI escape codes to reset terminal
    print("\033[!c\033[2J\033[H");
    return 0;
}
