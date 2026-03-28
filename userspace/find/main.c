#include <kyroolib.h>

#define MAX_PATH 512

static void find_recursive(const char *path, const char *name) {
    int dir = open(path, O_RDONLY);
    if (dir < 0) return;

    struct dirent de;
    int index = 0;

    while (syscall(SYS_READDIR, dir, index, (uint64_t)&de) > 0) {
        if (de.name[0] != '\0') {
            // Build full path
            char full_path[MAX_PATH];
            if (path[1] == '\0' && path[0] == '/') {
                strcpy(full_path, "/");
                strcat(full_path, de.name);
            } else {
                strcpy(full_path, path);
                strcat(full_path, "/");
                strcat(full_path, de.name);
            }

            // Check if matches name pattern
            if (strstr(de.name, name) != NULL) {
                print(full_path);
                print("\n");
            }

            // Recurse into directories (simple check - would need stat for proper impl)
            if (de.name[0] != '.') {
                find_recursive(full_path, name);
            }
        }
        index++;
    }

    close(dir);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        print("Usage: find <path> -name <pattern>\n");
        return 1;
    }

    const char *start_path = argv[1];
    const char *pattern = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) {
            pattern = argv[i + 1];
            break;
        }
    }

    if (!pattern) {
        pattern = argv[argc - 1];
    }

    find_recursive(start_path, pattern);
    return 0;
}
