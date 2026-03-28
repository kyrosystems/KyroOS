#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        print("Usage: ln [-s] <target> <link_name>\n");
        print("  -s: create symbolic link\n");
        return 1;
    }

    int symbolic = 0;
    int arg_start = 1;

    if (strcmp(argv[1], "-s") == 0) {
        symbolic = 1;
        arg_start = 2;
    }

    if (argc - arg_start < 2) {
        print("Usage: ln [-s] <target> <link_name>\n");
        return 1;
    }

    const char *target = argv[arg_start];
    const char *link_name = argv[arg_start + 1];

    if (symbolic) {
        // Create symbolic link - store target path in link file
        int fd = open(link_name, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) {
            print("ln: cannot create '");
            print(link_name);
            print("'\n");
            return 1;
        }

        write(fd, (uint8_t *)target, strlen(target));
        close(fd);

        print("ln: created symbolic link '");
        print(link_name);
        print("' -> '");
        print(target);
        print("'\n");
    } else {
        // Hard link - would need proper syscall support
        print("ln: hard links not yet supported\n");
        return 1;
    }

    return 0;
}
