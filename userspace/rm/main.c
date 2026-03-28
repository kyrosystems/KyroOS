#include <kyroolib.h>

int main(int argc, char **argv) {
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
