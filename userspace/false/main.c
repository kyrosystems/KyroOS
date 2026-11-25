#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: false\n");
        print("Return an exit status of 1 (failure).\n");
        print_flush();
        return 0;
    }
    (void)argc;
    (void)argv;

    // Always return failure
    return 1;
}
