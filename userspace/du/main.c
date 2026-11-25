#include <kyroolib.h>
#include <vfs.h>

static bool opt_human = false;
static bool opt_summary = false;
static bool opt_all = false;

static void format_size(uint64_t bytes, char *out) {
    if (!opt_human) {
        // du standard displays size in 1K blocks (rounded up to 4K filesystem blocks)
        uint64_t kblocks = (bytes + 1023) / 1024;
        if (kblocks == 0 && bytes > 0) kblocks = 1;
        sprintf(out, "%llu", kblocks);
        return;
    }
    if (bytes >= 1024ULL * 1024 * 1024) {
        sprintf(out, "%lluG", bytes / (1024ULL * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        sprintf(out, "%lluM", bytes / (1024 * 1024));
    } else if (bytes >= 1024) {
        sprintf(out, "%lluK", bytes / 1024);
    } else {
        sprintf(out, "%lluB", bytes);
    }
}

static uint64_t du_directory(const char *path, int depth) {
    if (depth > 16) return 0; // Prevent infinite recursion

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        // If it's a regular file, stat it directly
        struct stat st;
        if (stat(path, &st) == 0) {
            uint64_t sz = st.st_size;
            char sbuf[32];
            format_size(sz, sbuf);
            print(sbuf);
            print("\t");
            print(path);
            print("\n");
            return sz;
        }
        return 0;
    }

    uint64_t total_dir_size = 4096; // Directory node base size
    struct dirent de;
    int index = 0;

    while (readdir(fd, index++, &de) > 0) {
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;

        char subpath[512];
        if (strcmp(path, "/") == 0) {
            sprintf(subpath, "/%s", de.name);
        } else {
            sprintf(subpath, "%s/%s", path, de.name);
        }

        struct stat st;
        if (stat(subpath, &st) == 0) {
            if (st.st_mode & S_IFDIR) {
                uint64_t sub_sz = du_directory(subpath, depth + 1);
                total_dir_size += sub_sz;
                if (!opt_summary) {
                    char sbuf[32];
                    format_size(sub_sz, sbuf);
                    print(sbuf);
                    print("\t");
                    print(subpath);
                    print("\n");
                }
            } else {
                uint64_t file_sz = st.st_size;
                // Round to 4K block size
                uint64_t alloc_sz = (file_sz + 4095) & ~4095ULL;
                if (alloc_sz == 0 && file_sz > 0) alloc_sz = 4096;
                total_dir_size += alloc_sz;

                if (opt_all && !opt_summary) {
                    char sbuf[32];
                    format_size(alloc_sz, sbuf);
                    print(sbuf);
                    print("\t");
                    print(subpath);
                    print("\n");
                }
            }
        }
    }

    close(fd);
    return total_dir_size;
}

int main(int argc, char **argv) {
    const char *target = ".";
    opt_human = false;
    opt_summary = false;
    opt_all = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print("Usage: du [-h | --human-readable] [-s | --summary] [-a | --all] [PATH]\n");
            print("Estimate file and directory space usage.\n");
            print_flush();
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--human-readable") == 0) {
            opt_human = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--summary") == 0) {
            opt_summary = true;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            opt_all = true;
        } else if (argv[i][0] != '-') {
            target = argv[i];
        }
    }

    uint64_t total = du_directory(target, 0);
    char sbuf[32];
    format_size(total, sbuf);
    print(sbuf);
    print("\t");
    print(target);
    print("\n");
    print_flush();

    return 0;
}
