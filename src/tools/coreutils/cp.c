#include <errno.h>
#include <kyroolib.h>

#define BUF_SIZE 1024

#define BUF_SIZE 1024

// Helper function to recursively copy files and directories
static int copy_recursive(const char *src, const char *dest) {
    struct stat st;
    if (stat(src, &st) != 0) {
        print("cp: could not stat source '"); print(src); print("'\n");
        return -1;
    }

    if (S_ISREG(st.st_mode)) { // It's a regular file
        int src_fd = open(src, O_RDONLY);
        if (src_fd < 0) {
            print("cp: cannot open source file '"); print(src); print("'\n");
            return -1;
        }

        int dest_fd = open(dest, O_CREAT | O_TRUNC | O_WRONLY);
        if (dest_fd < 0) {
            print("cp: cannot create destination file '"); print(dest); print("'\n");
            close(src_fd);
            return -1;
        }

        char buffer[BUF_SIZE];
        int bytes_read;
        while ((bytes_read = read(src_fd, buffer, BUF_SIZE)) > 0) {
            if (write(dest_fd, buffer, bytes_read) < 0) {
                print("cp: write error to '"); print(dest); print("'\n");
                close(src_fd);
                close(dest_fd);
                return -1;
            }
        }
        close(src_fd);
        close(dest_fd);
        return 0;
    } else if (S_ISDIR(st.st_mode)) { // It's a directory
        int ret = mkdir(dest);
        if (ret < 0 && ret != -EEXIST) {
            print("cp: cannot create directory '"); print(dest); print("'\n");
            return -1;
        }

        int dir_fd = open(src, O_RDONLY); // Open source directory for reading entries
        if (dir_fd < 0) {
            print("cp: cannot open source directory '"); print(src); print("'\n");
            return -1;
        }

        struct dirent de;
        int index = 0;
        char src_entry_path[MAX_FILENAME_LEN * 2];
        char dest_entry_path[MAX_FILENAME_LEN * 2];

        while (readdir(dir_fd, index++, &de)) {
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
                continue;
            }

            sprintf(src_entry_path, "%s/%s", src, de.name);
            sprintf(dest_entry_path, "%s/%s", dest, de.name);
            
            if (copy_recursive(src_entry_path, dest_entry_path) != 0) {
                close(dir_fd);
                return -1; // Propagate error
            }
        }
        close(dir_fd);
        return 0;
    }
    print("cp: unsupported source type '"); print(src); print("'\n");
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        print("cp: usage: cp <source> <destination>\n");
        return 1;
    }

    const char *src_path = argv[1];
    const char *dest_path = argv[2];

    if (copy_recursive(src_path, dest_path) != 0) {
        return 1;
    }

    return 0;
}