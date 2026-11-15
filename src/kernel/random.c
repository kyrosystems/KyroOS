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

static uint64_t prng_state = 0x853c49e6748fea9bULL;

static inline uint64_t rdtsc_fallback(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void random_add_entropy(uint64_t entropy_value) {
    prng_state ^= (entropy_value * 0x9e3779b97f4a7c15ULL) ^ rdtsc_fallback();
}

static uint64_t splitmix64_next(void) {
    prng_state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = prng_state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static uint64_t random_word(void) {
    if (rng_available < 0) random_init();

    if (rng_available) {
        for (unsigned int attempt = 0; attempt < 16; attempt++) {
            uint64_t value;
            unsigned char ok;
            __asm__ __volatile__("rdrand %0; setc %1"
                                 : "=r"(value), "=qm"(ok));
            if (ok) return value;
            __asm__ __volatile__("pause");
        }
    }

    // Software fallback using TSC + SplitMix64
    random_add_entropy(rdtsc_fallback());
    return splitmix64_next();
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
