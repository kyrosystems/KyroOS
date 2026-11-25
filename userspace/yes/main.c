#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: yes [STRING]...\n");
        print("Output a string repeatedly until killed.\n");
        print_flush();
        return 0;
    }
    const char *output = (argc > 1) ? argv[1] : "y";

    while (1) {
        print(output);
        print("\n");
    }

    return 0;
}
