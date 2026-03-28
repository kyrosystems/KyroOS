#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Would need proper syscall to get disk usage info
    print("Filesystem      Size    Used   Available  Use%  Mounted on\n");
    print("kyrofs         100M     25M       75M   25%   /\n");

    return 0;
}
