#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: halt\n");
        print("Halt the machine.\n");
        print_flush();
        return 0;
    }
    (void)argc;
    (void)argv;

    print("halt: system halted\n");
    
    // HLT instruction or ACPI power off
    while (1) {
        __asm__ __volatile__("hlt");
    }
    
    return 0;
}
