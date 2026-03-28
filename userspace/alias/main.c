#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("Usage: alias [name=value]\n");
        return 0;
    }

    // Parse alias command
    const char *alias_str = argv[1];
    const char *eq = strchr(alias_str, '=');

    if (eq) {
        print("alias: '");
        // Print name part
        for (const char *p = alias_str; p < eq; p++) {
            char c = *p;
            write(1, (uint8_t *)&c, 1);
        }
        print("' set to '");
        print(eq + 1);
        print("'\n");
    } else {
        print("alias: ");
        print(alias_str);
        print("\n");
    }

    return 0;
}
