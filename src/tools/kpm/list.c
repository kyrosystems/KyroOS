#include "../../include/kpm.h"
#include "../kyroolib.h"

int pkg_list() {
    int fd = open(PKG_DB_PATH, O_RDONLY);
    if (fd < 0) {
        print("No packages installed.\n");
        return 0;
    }

    print("--- INSTALLED PACKAGES ---\n");
    
    char buf[1024];
    int n;
    while ((n = read(fd, buf, 1023)) > 0) {
        buf[n] = '\0';
        // This simple print assumes entries are newline-terminated
        // and doesn't handle partial lines between reads.
        // For a minimal implementation, this is acceptable.
        print(buf);
    }
    
    close(fd);
    return 0;
}
