#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: unalias <name>\n");
        return 1;
    }

    print("unalias: '");
    print(argv[1]);
    print("' removed\n");

    return 0;
}
