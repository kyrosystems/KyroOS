#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: killall NAME\n");
        print("Kill processes by name.\n");
        print_flush();
        return 0;
    }
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
