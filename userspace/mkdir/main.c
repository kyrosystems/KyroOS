#include <kyroolib.h>

static int mkdir_p(const char *path) {
    char tmp[256];
    char *p = NULL;
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    strcpy(tmp, path);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp); // ignore error if intermediate directory already exists
            *p = '/';
        }
    }
    return mkdir(tmp);
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: mkdir [-p] DIRECTORY...\n");
        print("Create DIRECTORY(ies) if they do not already exist.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        print("Usage: mkdir [-p] <directory...>\n");
        print_flush();
        return 1;
    }

    bool parents = false;
    int start_idx = 1;
    if (strcmp(argv[1], "-p") == 0) {
        parents = true;
        start_idx = 2;
    }

    if (start_idx >= argc) {
        print("mkdir: missing operand\n");
        print_flush();
        return 1;
    }

    int ret = 0;
    for (int i = start_idx; i < argc; i++) {
        int r = parents ? mkdir_p(argv[i]) : mkdir(argv[i]);
        if (r < 0) {
            printf("mkdir: cannot create directory '%s'\n", argv[i]);
            print_flush();
            ret = 1;
        }
    }
    return ret;
}
