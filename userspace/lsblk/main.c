#include <kyroolib.h>
#include <vfs.h>
#include <ide.h>

void lsblk() {
    int dev_fd = open("/dev", O_RDONLY);
    if (dev_fd < 0) {
        print("lsblk: cannot open /dev\n");
        return;
    }

    print("NAME      SIZE\n");

    struct dirent dir_entry;
    int index = 0;
    while (readdir(dev_fd, index++, &dir_entry) > 0) {
        char dev_path[256];
        sprintf(dev_path, "/dev/%s", dir_entry.name);

        int fd = open(dev_path, O_RDONLY);
        if (fd < 0) {
            continue; // Skip if we can't open it
        }

        ide_disk_info_t info;
        if (ioctl(fd, IDE_IOCTL_GET_DISK_INFO, &info) == 0) {
            // successful ioctl
            uint64_t size_mb = (info.total_sectors * info.bytes_per_sector) / (1024 * 1024);
            char out_buf[256];
            sprintf(out_buf, "%-9s %dM\n", dir_entry.name, size_mb);
            print(out_buf);
        }

        close(fd);
    }

    close(dev_fd);
}

int main() {
    lsblk();
    exit(0);
    return 0;
}
