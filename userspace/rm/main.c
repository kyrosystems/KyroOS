#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: rm [-r] [-f] FILE...\n");
        print("Remove files or directories.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        print("Usage: rm <file>\n");
        return 1;
    }

    if (unlink(argv[1]) < 0) {
        print("rm: failed to remove file\n");
        return 1;
    }
    return 0;
}
