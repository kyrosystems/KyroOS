#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: poweroff\n");
        print("Power down the system.\n");
        print_flush();
        return 0;
    }
    (void)argc;
    (void)argv;

    print("poweroff: system powering off...\n");
    
    // ACPI power off sequence would go here
    while (1) {
        __asm__ __volatile__("hlt");
    }
    
    return 0;
}
