#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: killall <process_name>\n");
        return 1;
    }

    print("killall: sending TERM to '");
    print(argv[1]);
    print("'\n");

    // Would need proper syscall to kill by name
    return 0;
}
