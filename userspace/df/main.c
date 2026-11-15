#include <kyroolib.h>
#include <vfs.h>
#include <ide.h>
#include <procinfo.h>

static void format_size(uint64_t bytes, char *out, bool human) {
    if (!human) {
        sprintf(out, "%10llu", bytes / 1024);
        return;
    }
    if (bytes >= 1024ULL * 1024 * 1024) {
        sprintf(out, "%4lluG", bytes / (1024ULL * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        sprintf(out, "%4lluM", bytes / (1024 * 1024));
    } else if (bytes >= 1024) {
        sprintf(out, "%4lluK", bytes / 1024);
    } else {
        sprintf(out, "%4lluB", bytes);
    }
}

int main(int argc, char **argv) {
    bool human = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print("Usage: df [-h | --human-readable] [--help]\n");
            print("Report filesystem disk space usage.\n");
            print_flush();
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--human-readable") == 0) {
            human = true;
        }
    }

    if (human) {
        print("Filesystem      Size  Used Avail Use% Mounted on\n");
    } else {
        print("Filesystem     1K-blocks      Used Available Use% Mounted on\n");
    }

    // 1. Root filesystem (/dev/sda1 or kyrofs)
    struct sysinfo_k si;
    int has_sysinfo = (sysinfo_k(&si) == 0);

    uint64_t root_total = 0;
    int sda_fd = open("/dev/sda1", O_RDONLY);
    if (sda_fd >= 0) {
        ide_disk_info_t info;
        if (ioctl(sda_fd, IDE_IOCTL_GET_DISK_INFO, &info) == 0 && info.total_sectors > 0) {
            root_total = (uint64_t)info.total_sectors * (info.bytes_per_sector ? info.bytes_per_sector : 512);
        }
        close(sda_fd);
    }
    if (root_total == 0 && has_sysinfo && si.fs_total_bytes > 0) {
        root_total = si.fs_total_bytes;
    }
    if (root_total == 0) {
        root_total = 100ULL * 1024 * 1024;
    }

    uint64_t root_used = (has_sysinfo && si.fs_used_bytes > 0) ? si.fs_used_bytes : (4096 * 64);

    uint64_t root_avail = (root_total > root_used) ? (root_total - root_used) : 0;
    uint32_t root_pct = root_total ? (uint32_t)((root_used * 100) / root_total) : 0;

    char s_tot[32], s_use[32], s_avl[32], line[256];
    format_size(root_total, s_tot, human);
    format_size(root_used, s_use, human);
    format_size(root_avail, s_avl, human);

    if (human) {
        sprintf(line, "%-14s %5s %5s %5s %3u%% /\n", "/dev/sda1", s_tot, s_use, s_avl, root_pct);
    } else {
        sprintf(line, "%-14s %10s %9s %9s %3u%% /\n", "/dev/sda1", s_tot, s_use, s_avl, root_pct);
    }
    print(line);

    // 2. devfs
    format_size(1024 * 1024, s_tot, human);
    format_size(128 * 1024, s_use, human);
    format_size(896 * 1024, s_avl, human);
    if (human) {
        sprintf(line, "%-14s %5s %5s %5s %3u%% /dev\n", "devfs", s_tot, s_use, s_avl, 12);
    } else {
        sprintf(line, "%-14s %10s %9s %9s %3u%% /dev\n", "devfs", s_tot, s_use, s_avl, 12);
    }
    print(line);

    // 3. tmpfs / RAM usage from sysinfo
    if (has_sysinfo && si.mem_total_kb > 0) {
        uint64_t mem_tot = (uint64_t)si.mem_total_kb * 1024;
        uint64_t mem_use = (uint64_t)si.mem_used_kb * 1024;
        uint64_t mem_avl = (mem_tot > mem_use) ? (mem_tot - mem_use) : 0;
        uint32_t mem_pct = mem_tot ? (uint32_t)((mem_use * 100) / mem_tot) : 0;

        format_size(mem_tot, s_tot, human);
        format_size(mem_use, s_use, human);
        format_size(mem_avl, s_avl, human);
        if (human) {
            sprintf(line, "%-14s %5s %5s %5s %3u%% /tmp\n", "tmpfs", s_tot, s_use, s_avl, mem_pct);
        } else {
            sprintf(line, "%-14s %10s %9s %9s %3u%% /tmp\n", "tmpfs", s_tot, s_use, s_avl, mem_pct);
        }
        print(line);
    }

    return 0;
}
