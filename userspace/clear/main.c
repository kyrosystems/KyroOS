#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // ANSI escape code to clear screen
    print("\033[2J\033[H");
    return 0;
}
