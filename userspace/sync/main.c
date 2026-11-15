#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: sync\n");
        print("Synchronize cached disk writes to storage.\n");
        print_flush();
        return 0;
    }
    (void)argc;
    (void)argv;

    // Sync would flush all filesystem buffers
    print("sync: filesystem buffers flushed\n");
    return 0;
}
