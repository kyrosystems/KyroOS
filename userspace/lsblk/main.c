#include <kyroolib.h>
#include <vfs.h>
#include <ide.h>

void lsblk(void) {
    int dev_fd = open("/dev", O_RDONLY);
    if (dev_fd < 0) {
        print("lsblk: cannot open /dev\n");
        return;
    }

    print("NAME       MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS\n");

    struct dirent dir_entry;
    int index = 0;
    while (readdir(dev_fd, index++, &dir_entry) > 0) {
        // Only inspect block devices: sda, sdb, ram, etc.
        if (strncmp(dir_entry.name, "sd", 2) != 0 &&
            strncmp(dir_entry.name, "hd", 2) != 0 &&
            strncmp(dir_entry.name, "ram", 3) != 0) {
            continue;
        }

        char dev_path[256];
        sprintf(dev_path, "/dev/%s", dir_entry.name);

        int fd = open(dev_path, O_RDONLY);
        if (fd < 0) continue;

        ide_disk_info_t info;
        if (ioctl(fd, IDE_IOCTL_GET_DISK_INFO, &info) == 0) {
            uint64_t bytes = (uint64_t)info.total_sectors * (info.bytes_per_sector ? info.bytes_per_sector : 512);
            char size_str[32];
            if (bytes >= 1024ULL * 1024 * 1024) {
                sprintf(size_str, "%uG", (uint32_t)(bytes / (1024ULL * 1024 * 1024)));
            } else if (bytes >= 1024 * 1024) {
                sprintf(size_str, "%uM", (uint32_t)(bytes / (1024 * 1024)));
            } else {
                sprintf(size_str, "%uK", (uint32_t)(bytes / 1024));
            }

            bool is_part = (dir_entry.name[2] >= '0' && dir_entry.name[2] <= '9') ||
                           (dir_entry.name[3] >= '0' && dir_entry.name[3] <= '9');

            char out_buf[256];
            if (is_part) {
                sprintf(out_buf, "`- %-7s 8:%d    0  %5s  0 part %s\n",
                        dir_entry.name, index, size_str, (strcmp(dir_entry.name, "sda1") == 0) ? "/" : "");
            } else {
                sprintf(out_buf, "%-10s 8:%d    0  %5s  0 disk\n",
                        dir_entry.name, index, size_str);
            }
            print(out_buf);
        }

        close(fd);
    }

    close(dev_fd);
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: lsblk\n");
        print("List information about block devices.\n");
        print_flush();
        return 0;
    }
    (void)argc;
    (void)argv;
    lsblk();
    return 0;
}
