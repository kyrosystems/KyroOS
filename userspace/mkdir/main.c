#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: mkdir <path>\n");
        return 1;
    }

    if (mkdir(argv[1]) < 0) {
        print("mkdir: failed to create directory\n");
        return 1;
    }
    return 0;
}
