#include <stddef.h> // For size_t
#include <stdint.h> // For uint32_t, uint64_t
#include "kyroolib.h" // For write and strlen syscall wrappers

// Standard file descriptors
#define STDOUT 1

void print(const char *s) {
    write(STDOUT, s, strlen(s));
}

static void print_digit(char d) {
    write(STDOUT, &d, 1);
}

void print_u32(uint32_t n) {
    if (n == 0) {
        print_digit('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        print_digit(buf[--i]);
    }
}

void print_u64(uint64_t n) {
    if (n == 0) {
        print_digit('0');
        return;
    }
    char buf[22];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        print_digit(buf[--i]);
    }
}

void print_hex(uint32_t n) {
    print("0x");
    if (n == 0) {
        print_digit('0');
        return;
    }
    char buf[10];
    int i = 0;
    const char *hex = "0123456789abcdef";
    while (n > 0) {
        buf[i++] = hex[n % 16];
        n /= 16;
    }
    while (i > 0) {
        print_digit(buf[--i]);
    }
}