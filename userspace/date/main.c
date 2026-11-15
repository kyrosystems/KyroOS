#include <kyroolib.h>

static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *months[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int get_day_of_week(int y, int m, int d) {
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: date\n");
        print("Print current system date and time from hardware RTC.\n");
        print_flush();
        return 0;
    }
    (void)argc;
    (void)argv;

    struct sysinfo_k si;
    if (sysinfo_k(&si) == 0 && si.rtc_year >= 2000 && si.rtc_mon >= 1 && si.rtc_mon <= 12) {
        int dow = get_day_of_week(si.rtc_year, si.rtc_mon, si.rtc_day);
        if (dow < 0 || dow > 6) dow = 0;
        const char *day_name = days[dow];
        const char *mon_name = months[si.rtc_mon];
        printf("%s %s %2d %02d:%02d:%02d UTC %d\n", day_name, mon_name, si.rtc_day, si.rtc_hour, si.rtc_min, si.rtc_sec, si.rtc_year);
    } else {
        // Fallback to uptime calculation if RTC not ready
        uint64_t ticks = syscall(SYS_GET_TICKS, 0, 0, 0);
        uint64_t seconds = ticks / 100;
        uint64_t hrs = (seconds / 3600) % 24;
        uint64_t mins = (seconds / 60) % 60;
        uint64_t secs = seconds % 60;
        printf("Thu Jan  1 %02d:%02d:%02d UTC 1970\n", (int)hrs, (int)mins, (int)secs);
    }

    return 0;
}
