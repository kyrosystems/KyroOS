#include <kyroolib.h>

int main(int argc, char **argv) {
    char *new_argv[32];
    new_argv[0] = "/bin/cube3d";
    for (int i = 1; i < argc && i < 31; i++) {
        new_argv[i] = argv[i];
    }
    new_argv[argc] = NULL;
    return execve("/bin/cube3d", new_argv, NULL);
}
