#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: export [NAME=VALUE]\n");
        print("Set export attribute for shell variables.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        print("Usage: export <variable=value>\n");
        return 1;
    }

    print("export: '");
    print(argv[1]);
    print("' exported\n");

    return 0;
}
