#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: rmdir DIRECTORY...\n");
        print("Remove empty directories.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        print("Usage: rmdir <path>\n");
        return 1;
    }

    if (rmdir(argv[1]) < 0) {
        print("rmdir: failed to remove directory\n");
        return 1;
    }
    return 0;
}
