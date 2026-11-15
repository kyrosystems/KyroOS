#include <kyroolib.h>
#include <stdint.h>

#define MAX_PROCS 128

static const char *state_to_str(uint8_t state) {
    switch (state) {
        case PROC_STATE_RUNNING: return "R";
        case PROC_STATE_READY:   return "S";
        case PROC_STATE_BLOCKED: return "D";
        case PROC_STATE_DEAD:    return "Z";
        default:                 return "?";
    }
}

int main(int argc, char **argv) {
    bool full = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print("Usage: ps [-a] [-u] [-f]\n");
            print("Report a snapshot of the current processes.\n");
            print_flush();
            return 0;
        }
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-u") == 0) full = true;
    }

    struct procinfo procs[MAX_PROCS];
    int n = proc_list(procs, MAX_PROCS);
    if (n < 0) {
        print("ps: error retrieving process list\n");
        print_flush();
        return 1;
    }

    if (full) {
        printf("%-5s %-5s %-4s %-8s %-10s %s\n", "UID", "PID", "STAT", "TICKS", "TIME", "CMD");
        for (int i = 0; i < n; i++) {
            uint32_t sec = (uint32_t)(procs[i].cpu_ticks / 100);
            uint32_t min = sec / 60;
            sec %= 60;
            char time_buf[16];
            snprintf(time_buf, sizeof(time_buf), "%02u:%02u", min, sec);

            printf("%-5u %-5u %-4s %-8llu %-10s %s\n",
                   procs[i].uid,
                   (uint32_t)procs[i].pid,
                   state_to_str(procs[i].state),
                   procs[i].cpu_ticks,
                   time_buf,
                   procs[i].name[0] ? procs[i].name : "[kernel]");
        }
    } else {
        printf("%5s %-5s %8s %s\n", "PID", "STAT", "TIME", "COMMAND");
        for (int i = 0; i < n; i++) {
            uint32_t sec = (uint32_t)(procs[i].cpu_ticks / 100);
            uint32_t min = sec / 60;
            sec %= 60;
            char time_buf[16];
            snprintf(time_buf, sizeof(time_buf), "%02u:%02u", min, sec);

            printf("%5u %-5s %8s %s\n",
                   (uint32_t)procs[i].pid,
                   state_to_str(procs[i].state),
                   time_buf,
                   procs[i].name[0] ? procs[i].name : "[kernel]");
        }
    }
    print_flush();
    return 0;
}
