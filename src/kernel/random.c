#include "random.h"
#include "kstring.h"
#include "log.h"
#include <stddef.h>
#include <stdint.h>

static int rng_available = -1;

void random_init(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = 1;
    ecx = 0;
    __asm__ __volatile__("cpuid"
                         : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    rng_available = (ecx & (1U << 30)) != 0;
    if (!rng_available)
        klog(LOG_ERROR, "RNG: RDRAND unavailable; secure random output disabled.");
}

void random_add_entropy(uint64_t entropy_value) {
    (void)entropy_value;
}

static uint64_t random_word(void) {
    if (rng_available < 0) random_init();
    if (!rng_available)
        panic("RNG: no cryptographic hardware RNG available", NULL);

    for (unsigned int attempt = 0; attempt < 16; attempt++) {
        uint64_t value;
        unsigned char ok;
        __asm__ __volatile__("rdrand %0; setc %1"
                             : "=r"(value), "=qm"(ok));
        if (ok) return value;
        __asm__ __volatile__("pause");
    }
    panic("RNG: RDRAND failed", NULL);
    __builtin_unreachable();
}

uint64_t random_get_uint64(void) {
    return random_word();
}

void random_get_bytes(void *buffer, size_t num_bytes) {
    if (!buffer && num_bytes != 0)
        panic("RNG: null output buffer", NULL);

    uint8_t *dst = (uint8_t *)buffer;
    while (num_bytes != 0) {
        uint64_t word = random_word();
        size_t chunk = num_bytes < sizeof(word) ? num_bytes : sizeof(word);
        memcpy(dst, &word, chunk);
        dst += chunk;
        num_bytes -= chunk;
    }
}
