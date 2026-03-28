#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Get current time from RTC or system
    uint64_t ticks = syscall(SYS_GET_TICKS, 0, 0, 0);
    uint64_t seconds = ticks / 100;
    
    uint64_t hrs = (seconds / 3600) % 24;
    uint64_t mins = (seconds / 60) % 60;
    uint64_t secs = seconds % 60;
    
    // Simple date output (would need RTC for real date)
    print("Wed Mar 18 ");
    
    // Print hours
    if (hrs < 10) print("0");
    print_u32((uint32_t)hrs);
    print(":");
    
    // Print minutes
    if (mins < 10) print("0");
    print_u32((uint32_t)mins);
    print(":");
    
    // Print seconds
    if (secs < 10) print("0");
    print_u32((uint32_t)secs);
    print(" 2026\n");
    
    return 0;
}
