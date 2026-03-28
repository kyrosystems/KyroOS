#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint64_t ticks = syscall(SYS_GET_TICKS, 0, 0, 0);
    uint64_t seconds = ticks / 100;

    uint64_t days = seconds / 86400;
    uint64_t hours = (seconds % 86400) / 3600;
    uint64_t mins = (seconds % 3600) / 60;
    uint64_t secs = seconds % 60;

    print("up ");
    if (days > 0) {
        print_u32(days);
        print(" days, ");
    }
    print_u32(hours);
    print(" hours, ");
    print_u32(mins);
    print(" minutes, ");
    print_u32(secs);
    print(" seconds\n");

    return 0;
}
