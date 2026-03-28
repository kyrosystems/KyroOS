#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: unset <variable>\n");
        return 1;
    }

    print("unset: '");
    print(argv[1]);
    print("' unset\n");

    return 0;
}
