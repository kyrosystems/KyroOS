#include <kyroolib.h>

#define BUF_SIZE 1024

int main(int argc, char **argv) {
    if (argc < 2) {
        print("cat: usage: cat <file>\n");
        return 1;
    }

    const char *file_path = argv[1];

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        print("cat: cannot open '" );
        print(file_path);
        print("'\n");
        return 1;
    }

    char buffer[BUF_SIZE];
    int bytes_read;
    while ((bytes_read = read(fd, buffer, BUF_SIZE)) > 0) {
        // Assuming print can handle arbitrary binary data and prints to stdout
        // For text files, this is fine.
        write(1, buffer, bytes_read); // Use write to stdout (fd 1)
    }

    close(fd);
    return 0;
}