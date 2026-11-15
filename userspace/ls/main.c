#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: ls [-l] [-a] [FILE]...\n");
        print("List directory contents.\n");
        print_flush();
        return 0;
    }
    const char *path = ".";
    if (argc > 1) {
        path = argv[1];
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("ls: cannot access '%s': No such file or directory\n", path);
        print_flush();
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        // Regular file: print name with color
        if (st.st_mode & 0111) {
            printf("\033[1;32m%s\033[0m\n", path);
        } else {
            printf("%s\n", path);
        }
        print_flush();
        return 0;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("ls: cannot open directory '%s'\n", path);
        print_flush();
        return 1;
    }

    struct dirent entry;
    int i = 0;
    int count = 0;
    while (readdir(fd, i++, &entry) > 0) {
        if (entry.name[0] == '\0') continue;

        // Build full subpath for stat check
        char subpath[512];
        if (strcmp(path, "/") == 0) {
            snprintf(subpath, sizeof(subpath), "/%s", entry.name);
        } else if (strcmp(path, ".") == 0) {
            snprintf(subpath, sizeof(subpath), "%s", entry.name);
        } else {
            snprintf(subpath, sizeof(subpath), "%s/%s", path, entry.name);
        }

        struct stat sub_st;
        if (stat(subpath, &sub_st) == 0 && S_ISDIR(sub_st.st_mode)) {
            // Directory: Blue bold with trailing slash
            printf("\033[1;34m%s/\033[0m  ", entry.name);
        } else if (sub_st.st_mode & 0111) {
            // Executable: Green bold
            printf("\033[1;32m%s\033[0m  ", entry.name);
        } else {
            printf("%s  ", entry.name);
        }
        count++;
    }

    if (count > 0) {
        print("\n");
    }
    print_flush();

    close(fd);
    return 0;
}
