#include <kyroolib.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // In a real system, this would query user/group info
    print("uid=0(root) gid=0(root) groups=0(root)\n");
    return 0;
}
