#include <kyroolib.h>

#include <kyroolib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        print("cd: usage: cd <directory>\n");
        return 1;
    }
    print("cd: Changing directory to ");
    print(argv[1]);
    print("\n");
    print("cd: (Changing current directory not yet implemented at syscall level)\n");
    return 0;
}

