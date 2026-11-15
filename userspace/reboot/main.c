#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: reboot\n");
        print("Reboot the system.\n");
        print_flush();
        return 0;
    }
    print("Rebooting...\n");
    // Pulse the CPU reset line via the keyboard controller
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint8_t)0x64));
    // If that fails (e.g. in some VMs), try another common method
    for(volatile int i = 0; i < 10000; i++);
    __asm__ __volatile__("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604)); // QEMU ACPI shutdown/reboot
    return 0;
}
