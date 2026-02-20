#include <kyroolib.h>
#include <vfs.h> // For MAX_FILENAME_LEN

int main(int argc, char **argv) {
    char path[MAX_FILENAME_LEN];
    if (argc > 1) {
        strcpy(path, argv[1]);
    } else {
        strcpy(path, "."); // Current directory
    }

    int dir_fd = open(path, O_RDONLY);
    if (dir_fd < 0) {
        print("ls: cannot access '");
        print(path);
        print("': No such file or directory\n");
        return 1;
    }

    struct dirent de;
    int index = 0;
    while (readdir(dir_fd, index++, &de)) {
        print(de.name);
        print("\n");
    }

    close(dir_fd);
    return 0;
}