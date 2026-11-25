#include <kyroolib.h>
#include <stdbool.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: free [-h | -m | -k]\n");
        print("Display amount of free and used memory in the system.\n");
        print_flush();
        return 0;
    }
    bool in_mb = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0) in_mb = false;
        else if (strcmp(argv[i], "-m") == 0) in_mb = true;
        else if (strcmp(argv[i], "-h") == 0) in_mb = true;
    }

    struct sysinfo_k si;
    if (sysinfo_k(&si) != 0) {
        print("free: cannot read system memory info\n");
        return 1;
    }

    uint32_t total = in_mb ? (si.mem_total_kb / 1024) : si.mem_total_kb;
    uint32_t used = in_mb ? (si.mem_used_kb / 1024) : si.mem_used_kb;
    uint32_t free_mem = (total >= used) ? (total - used) : 0;

    printf("               total        used        free      shared  buff/cache   available\n");
    if (in_mb) {
        printf("Mem:     %8u MB %8u MB %8u MB           0           0 %8u MB\n",
               total, used, free_mem, free_mem);
    } else {
        printf("Mem:     %8u KB %8u KB %8u KB           0           0 %8u KB\n",
               total, used, free_mem, free_mem);
    }
    printf("Swap:            0           0           0\n");
    print_flush();
    return 0;
}
