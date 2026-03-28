#include <kyroolib.h>

int main(int argc, char **argv) {
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
