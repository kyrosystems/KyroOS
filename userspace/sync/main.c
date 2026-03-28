#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Sync would flush all filesystem buffers
    print("sync: filesystem buffers flushed\n");
    return 0;
}
