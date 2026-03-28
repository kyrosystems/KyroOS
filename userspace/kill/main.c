#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: kill [-signal] <pid>\n");
        print("Signals:\n");
        print("  -1  HUP   - Hangup\n");
        print("  -9  KILL  - Kill (default)\n");
        print("  -15 TERM  - Terminate\n");
        return 1;
    }

    int signal = 9; // Default SIGKILL
    int pid_arg = 1;

    if (argv[1][0] == '-') {
        if (strcmp(argv[1], "-9") == 0 || strcmp(argv[1], "-KILL") == 0) {
            signal = 9;
        } else if (strcmp(argv[1], "-15") == 0 || strcmp(argv[1], "-TERM") == 0) {
            signal = 15;
        } else if (strcmp(argv[1], "-1") == 0 || strcmp(argv[1], "-HUP") == 0) {
            signal = 1;
        }
        pid_arg = 2;
    }

    if (argc <= pid_arg) {
        print("kill: missing PID\n");
        return 1;
    }

    // Parse PID
    int pid = 0;
    const char *pid_str = argv[pid_arg];
    for (int i = 0; pid_str[i] != '\0'; i++) {
        if (pid_str[i] >= '0' && pid_str[i] <= '9') {
            pid = pid * 10 + (pid_str[i] - '0');
        } else {
            print("kill: invalid PID '");
            print(pid_str);
            print("'\n");
            return 1;
        }
    }

    // Would need proper kill syscall with signal support
    print("kill: sending signal ");
    print_u32(signal);
    print(" to process ");
    print_u32(pid);
    print("\n");

    return 0;
}
