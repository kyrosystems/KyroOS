#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("halt: system halted\n");
    
    // HLT instruction or ACPI power off
    while (1) {
        __asm__ __volatile__("hlt");
    }
    
    return 0;
}
