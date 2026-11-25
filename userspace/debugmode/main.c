#include <kyroolib.h>

/* debugmode on|off|status - controls the global kernel debug flag
 * (epstein) through SYS_DEBUGMODE_GET / SYS_DEBUGMODE_SET. Setting
 * requires euid 0; the kernel enforces this, not the shell. */

static void usage(void) {
    print("Usage: debugmode on|off|status\n");
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: debugmode [0|1]\n");
        print("Get or set kernel debug mode.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        usage();
        return 2;
    }

    if (strcmp(argv[1], "status") == 0) {
        int v = debugmode_get();
        print("debugmode = ");
        print(v ? "on" : "off");
        print("\n");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "off") == 0) {
        int want = (strcmp(argv[1], "on") == 0) ? 1 : 0;
        if (debugmode_set(want) != 0) {
            print("debugmode: permission denied (requires root)\n");
            return 1;
        }
        int actual = debugmode_get();
        print("debugmode: set to ");
        print((want ? "on" : "off"));
        print(", verified = ");
        print(actual ? "on" : "off");
        print("\n");
        return (actual == want) ? 0 : 1;
    }

    usage();
    return 2;
}
