#include <kyroolib.h>

int main(int argc, char **argv) {
    int show_all = 0;
    int show_kernel = 0;
    int show_nodename = 0;
    int show_release = 0;
    int show_version = 0;
    int show_machine = 0;
    int show_processor = 0;

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-a") == 0) show_all = 1;
            else if (strcmp(argv[i], "-s") == 0) show_kernel = 1;
            else if (strcmp(argv[i], "-n") == 0) show_nodename = 1;
            else if (strcmp(argv[i], "-r") == 0) show_release = 1;
            else if (strcmp(argv[i], "-v") == 0) show_version = 1;
            else if (strcmp(argv[i], "-m") == 0) show_machine = 1;
            else if (strcmp(argv[i], "-p") == 0) show_processor = 1;
        }
    }

    if (!show_all && !show_kernel && !show_nodename && !show_release && 
        !show_version && !show_machine && !show_processor) {
        show_all = 1;
    }

    if (show_all || show_kernel) {
        print("KyroOS");
    }
    if (show_all || show_nodename) {
        print(" localhost");
    }
    if (show_all || show_release) {
        print(" 26.03.12");
    }
    if (show_all || show_version) {
        print(" Titanium");
    }
    if (show_all || show_machine) {
        print(" x86_64");
    }
    if (show_all || show_processor) {
        print(" x86_64");
    }

    print("\n");
    return 0;
}
