#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: export <variable=value>\n");
        return 1;
    }

    print("export: '");
    print(argv[1]);
    print("' exported\n");

    return 0;
}
