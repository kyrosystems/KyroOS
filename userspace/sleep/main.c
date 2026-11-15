#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: sleep NUMBER[s]\n");
        print("Pause for NUMBER seconds.\n");
        print_flush();
        return 0;
    }
    if (argc < 2) {
        print("Usage: sleep <seconds>\n");
        return 1;
    }

    // Parse seconds
    uint32_t seconds = 0;
    const char *s = argv[1];
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            seconds = seconds * 10 + (s[i] - '0');
        }
    }

    sleep(seconds);

    return 0;
}
