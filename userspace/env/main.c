#include <kyroolib.h>

#define MAX_ENV 64
#define MAX_ENV_LEN 256

static char env_vars[MAX_ENV][MAX_ENV_LEN];
static int env_count = 0;

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Initialize with some default environment
    strcpy(env_vars[env_count++], "PATH=/bin:/usr/bin");
    strcpy(env_vars[env_count++], "HOME=/root");
    strcpy(env_vars[env_count++], "USER=root");
    strcpy(env_vars[env_count++], "SHELL=/bin/kyroshell");
    strcpy(env_vars[env_count++], "TERM=kyroos");
    strcpy(env_vars[env_count++], "HOSTNAME=localhost");
    strcpy(env_vars[env_count++], "OS=KyroOS");

    // Print all environment variables
    for (int i = 0; i < env_count; i++) {
        print(env_vars[i]);
        print("\n");
    }

    return 0;
}
