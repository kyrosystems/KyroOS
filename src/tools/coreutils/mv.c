#include <kyroolib.h>

#define BUF_SIZE 1024

// Helper function to recursively move files and directories (copy + delete)
static int move_recursive(const char *src, const char *dest) {
    struct stat st;
    if (stat(src, &st) != 0) {
        print("mv: could not stat source '"); print(src); print("'\n");
        return -1;
    }

    if (S_ISREG(st.st_mode)) { // It's a regular file
        // Copy the file
        int src_fd = open(src, O_RDONLY);
        if (src_fd < 0) {
            print("mv: cannot open source file '"); print(src); print("'\n");
            return -1;
        }

        int dest_fd = open(dest, O_CREAT | O_TRUNC | O_WRONLY);
        if (dest_fd < 0) {
            print("mv: cannot create destination file '"); print(dest); print("'\n");
            close(src_fd);
            return -1;
        }

        char buffer[BUF_SIZE];
        int bytes_read;
        while ((bytes_read = read(src_fd, buffer, BUF_SIZE)) > 0) {
            if (write(dest_fd, buffer, bytes_read) < 0) {
                print("mv: write error to '"); print(dest); print("'\n");
                close(src_fd);
                close(dest_fd);
                return -1;
            }
        }
        close(src_fd);
        close(dest_fd);

        // Delete the original file
        if (unlink(src) != 0) {
            print("mv: warning: failed to remove original file '"); print(src); print("'\n");
        }
        return 0;
    } else if (S_ISDIR(st.st_mode)) { // It's a directory
        if (mkdir(dest) != 0) {
            // Check if it already exists as a directory
            struct stat dest_st;
            if (stat(dest, &dest_st) != 0 || !S_ISDIR(dest_st.st_mode)) {
                print("mv: cannot create directory '"); print(dest); print("'\n");
                return -1;
            }
        }

        int dir_fd = open(src, O_RDONLY); // Open source directory for reading entries
        if (dir_fd < 0) {
            print("mv: cannot open source directory '"); print(src); print("'\n");
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

            ksprintf(src_entry_path, "%s/%s", src, de.name);
            ksprintf(dest_entry_path, "%s/%s", dest, de.name);
            
            if (move_recursive(src_entry_path, dest_entry_path) != 0) {
                close(dir_fd);
                return -1; // Propagate error
            }
        }
        close(dir_fd);

        // Remove the now empty source directory
        if (rmdir(src) != 0) {
            print("mv: warning: failed to remove source directory '"); print(src); print("'\n");
        }
        return 0;
    }
    print("mv: unsupported source type '"); print(src); print("'\n");
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        print("mv: usage: mv <source> <destination>\n");
        return 1;
    }

    const char *src_path = argv[1];
    const char *dest_path = argv[2];

    if (move_recursive(src_path, dest_path) != 0) {
        return 1;
    }

    return 0;
}