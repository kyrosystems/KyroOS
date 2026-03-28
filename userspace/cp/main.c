#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        print("Usage: cp <source> <destination>\n");
        return 1;
    }

    const char *src = argv[1];
    const char *dst = argv[2];

    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) {
        print("cp: cannot open '");
        print(src);
        print("': No such file or directory\n");
        return 1;
    }

    int fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd_dst < 0) {
        print("cp: cannot create '");
        print(dst);
        print("'\n");
        close(fd_src);
        return 1;
    }

    char buf[512];
    int bytes_read;
    while ((bytes_read = read(fd_src, buf, sizeof(buf))) > 0) {
        write(fd_dst, buf, bytes_read);
    }

    close(fd_src);
    close(fd_dst);
    return 0;
}
