#include <kyroolib.h>

int main(int argc, char **argv) {
    const char *output = (argc > 1) ? argv[1] : "y";

    while (1) {
        print(output);
        print("\n");
    }

    return 0;
}
