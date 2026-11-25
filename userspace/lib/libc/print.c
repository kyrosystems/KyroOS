#include <stddef.h> // For size_t
#include <stdint.h> // For uint32_t, uint64_t
#include "kyroolib.h" // For write and strlen syscall wrappers

/*
 * Buffered console output.
 *
 * Every print() used to issue one write() syscall — and one framebuffer
 * flush inside the kernel — per call (worst case per character). That made
 * TUI apps (sysmon, editor) render line-by-line/char-by-char at a crawl.
 * Output now accumulates in a line-sized buffer and is flushed on newline,
 * when the buffer fills, or explicitly via print_flush().
 *
 * Concurrency note: while a userspace program runs it holds the console
 * input focus and the kernel shell suppresses its own prompt, so a single
 * printing thread owns this buffer in practice.
 */
#define STDOUT 1

static char pbuf[2048];
static unsigned int plen = 0;

void print_flush(void) {
    if (plen == 0) return;
    write(STDOUT, pbuf, plen);
    plen = 0;
}

static void pputc(char c) {
    pbuf[plen++] = c;
    if (c == '\n' || plen >= sizeof(pbuf) - 1) print_flush();
}

void print(const char *s) {
    if (!s) return;
    while (*s) pputc(*s++);
}

void print_u32(uint32_t n) {
    char buf[12];
    int i = 0;
    if (n == 0) { pputc('0'); return; }
    while (n > 0) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i > 0) pputc(buf[--i]);
}

void print_u64(uint64_t n) {
    char buf[22];
    int i = 0;
    if (n == 0) { pputc('0'); return; }
    while (n > 0) { buf[i++] = (char)('0' + (n % 10)); n /= 10; }
    while (i > 0) pputc(buf[--i]);
}

void print_hex(uint32_t n) {
    print("0x");
    if (n == 0) { pputc('0'); return; }
    char buf[10];
    int i = 0;
    const char *hex = "0123456789abcdef";
    while (n > 0) { buf[i++] = hex[n % 16]; n /= 16; }
    while (i > 0) pputc(buf[--i]);
}
