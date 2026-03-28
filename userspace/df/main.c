#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("Filesystem      Size    Used   Available  Use%  Mounted on\n");
    print("kyrofs         100M     25M       75M   25%   /\n");

    // Would need proper syscall to get actual disk info
    return 0;
}
