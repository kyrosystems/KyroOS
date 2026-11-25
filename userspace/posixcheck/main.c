#include <kyroolib.h>
#include <errno.h>

static int failures = 0;

static void check(bool ok, const char *name) {
    print(ok ? "PASS " : "FAIL ");
    print(name);
    print("\n");
    if (!ok) failures++;
}

int main(void) {
    char byte = 0;
    check(read(-1, &byte, 1) == -1 && errno == EBADF, "invalid descriptor errno");
    check(execve((const char *)0x0000800000000000ULL, NULL, NULL) == -1 && errno == EFAULT,
          "noncanonical exec path rejected");
    void *p = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(p != MAP_FAILED, "anonymous mmap");
    if (p != MAP_FAILED) {
        uint8_t *bytes = p;
        bool zero = true;
        for (int i = 0; i < 8192; i++) if (bytes[i]) zero = false;
        check(zero, "mapping is zero filled");
        bytes[0] = 0x42; bytes[8191] = 0x24;
        check(mprotect(p, 8192, PROT_READ) == 0, "read-only protection");
        check(bytes[0] == 0x42 && bytes[8191] == 0x24, "protection preserves data");
        int zero_fd = open("/dev/zero", O_RDONLY);
        check(zero_fd >= 0 && read(zero_fd, p, 1) == -1 && errno == EFAULT, "read rejects protected output buffer");
        if (zero_fd >= 0) close(zero_fd);
        check(mprotect(p, 4096, PROT_NONE) == 0, "PROT_NONE");
        check(mprotect(p, 8192, PROT_READ | PROT_WRITE) == 0, "restore access");
        bytes[0] = 0x55;
        check(mprotect((void *)((uintptr_t)p + 1), 4096, PROT_READ) == -1,
              "unaligned mprotect rejected");
        check(munmap(p, 8192) == 0, "munmap");
        check(mprotect(p, 8192, PROT_READ) == -1, "unmapped mprotect rejected");
    }
    check(munmap((void *)0xFFFFFFFF80000000ULL, 4096) == -1,
          "cannot unmap kernel");
    check(mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED,
          "unsupported sharing fails explicitly");
    check(mmap(NULL, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED,
          "zero length mmap rejected");
    char test_path[80];
    sprintf(test_path, "/tmp/posixcheck-%u.txt", getuid());
    int fd = open(test_path, O_CREAT | O_TRUNC | O_RDWR);
    check(fd >= 0, "create test file");
    if (fd >= 0) {
        check(write(fd, "abcd", 4) == 4, "write file");
        lseek(fd, 0, 0);
        int copy = dup(fd);
        char c = 0;
        check(copy >= 0, "dup file");
        check(read(fd, &c, 1) == 1 && c == 'a', "read original descriptor");
        check(read(copy, &c, 1) == 1 && c == 'b', "dup shares file offset");
        close(fd);
        check(read(copy, &c, 1) == 1 && c == 'c', "dup survives original close");
        close(copy);
        unlink(test_path);
    }
    int ends[2];
    if (pipe(ends) == 0) {
        int writer = dup(ends[1]);
        close(ends[1]);
        char c = 0;
        check(write(writer, "p", 1) == 1, "duplicated pipe stays open");
        close(writer);
        check(read(ends[0], &c, 1) == 1 && c == 'p', "pipe buffered data");
        check(read(ends[0], &c, 1) == 0, "pipe EOF after last writer");
        close(ends[0]);
    } else check(false, "create pipe");
    int status = 0;
    char *args[] = {"/bin/false", NULL};
    int child = execve(args[0], args, NULL);
    check(child >= 0, "spawn child");
    if (child >= 0) {
        check(waitpid(-1, &status, 0) == child, "wait for any child");
        check(WIFEXITED(status) && WEXITSTATUS(status) == 1, "encoded child exit status");
    }
    check(waitpid(getpid(), &status, WNOHANG) == -1, "cannot wait for self");
    print_flush();
    // Console marker lets the host test observe results even under redirection
    int console = open("/dev/console", O_WRONLY);
    if (console >= 0) {
        const char *result = failures ? "POSIXCHECK: FAIL\n" : "POSIXCHECK: PASS\n";
        write(console, result, strlen(result));
        close(console);
    }
    return failures ? 1 : 0;
}
