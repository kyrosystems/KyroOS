#include <kyroolib.h>
#include <stddef.h> // For size_t
#include <string.h> // For strlen

// Assumes `write` function is available from kyroolib.h (which it is, as inline syscall)
void print(const char* s) {
    if (s == NULL) {
        return; // Or handle as an empty string. kpm uses it for messages, so empty is fine.
    }
    size_t len = strlen(s);
    write(1, s, len); // write to stdout (fd 1)
}
