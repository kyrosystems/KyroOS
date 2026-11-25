#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc != 5) return 2;
    uint32_t uid = (uint32_t)atoi(argv[1]);
    uint32_t gid = (uint32_t)atoi(argv[2]);
    // Only this child drops credentials; init stays privileged for the next login
    if (setgid(gid) < 0 || setuid(uid) < 0) return 1;
    if (chdir(argv[3]) < 0) chdir("/");
    char *args[] = {argv[4], NULL};
    int pid = execve(args[0], args, NULL);
    if (pid < 0 && strcmp(args[0], "/bin/sh") != 0) {
        args[0] = "/bin/sh";
        pid = execve(args[0], args, NULL);
    }
    if (pid < 0) return 1;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
