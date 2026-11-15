#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: unset NAME\n");
        print("Unset values and attributes of variables.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        print("Usage: unset <variable>\n");
        return 1;
    }

    print("unset: '");
    print(argv[1]);
    print("' unset\n");

    return 0;
}
