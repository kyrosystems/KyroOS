#include <kyroolib.h>
#include <stddef.h> // For size_t
#include <string.h> // For strlen

int puts(const char* s) {
    if (s == NULL) {
        // According to POSIX, puts() with NULL behavior is undefined.
        // We'll treat it as writing an empty string followed by a newline.
        return write(1, "\n", 1);
    }

    size_t len = strlen(s);
    long ret = write(1, s, len);
    if (ret < 0) {
        return -1; // Error during write
    }

    // Always append a newline
    long newline_ret = write(1, "\n", 1);
    if (newline_ret < 0) {
        return -1; // Error writing newline
    }

    return (int)(ret + newline_ret); // Return total bytes written (including newline)
}
