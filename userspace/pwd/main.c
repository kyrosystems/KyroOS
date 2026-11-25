#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: pwd\n");
        print("Print name of current/working directory.\n");
        print_flush();
        return 0;
    }
    print("/\n");
    return 0;
}
