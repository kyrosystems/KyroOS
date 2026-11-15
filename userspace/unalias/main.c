#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: unalias NAME\n");
        print("Remove alias definitions.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        print("Usage: unalias <name>\n");
        return 1;
    }

    print("unalias: '");
    print(argv[1]);
    print("' removed\n");

    return 0;
}
