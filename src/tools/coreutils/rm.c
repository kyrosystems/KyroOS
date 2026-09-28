#include <kyroolib.h>

#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("rm: usage: rm <file>\n");
        return 1;
    }

    const char *file_path = argv[1];

    if (unlink(file_path) < 0) {
        print("rm: failed to remove '");
        print(file_path);
        print("'\n");
        return 1;
    }

    return 0;
}