#include <kyroolib.h>

#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("mkdir: usage: mkdir <directory>\n");
        return 1;
    }

    const char *dir_path = argv[1];

    if (mkdir(dir_path) < 0) {
        print("mkdir: failed to create directory '");
        print(dir_path);
        print("'\n");
        return 1;
    }

    return 0;
}