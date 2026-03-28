#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("poweroff: system powering off...\n");
    
    // ACPI power off sequence would go here
    while (1) {
        __asm__ __volatile__("hlt");
    }
    
    return 0;
}
