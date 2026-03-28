#include <kyroolib.h>

int main(int argc, char **argv) {
    const char *path = ".";
    if (argc > 1) {
        path = argv[1];
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("ls: cannot open directory\n");
        return 1;
    }

    struct dirent entry;
    int i = 0;
    while (readdir(fd, i++, &entry) == 0) {
        print(entry.name);
        print("  ");
    }
    print("\n");

    close(fd);
    return 0;
}
