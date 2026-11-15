#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: hello\n");
        print("Print greeting message.\n");
        print_flush();
        return 0;
    }
    print("Hello from userspace!\n");
    exit(0);
    return 0;
}
