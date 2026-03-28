#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // ANSI escape codes to reset terminal
    print("\033[!c\033[2J\033[H");
    return 0;
}
