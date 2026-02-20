#include "random.h"
#include "log.h"
#include "isr.h" // For timer_get_ticks, enable/disable interrupts
#include "kstring.h" // For memcpy, memset

// Xorshift128+ state
static uint64_t s[2];
static bool initialized = false;

// Forward declaration for the internal PRNG function
static uint64_t xorshift128p(void);

void random_init(void) {
    // Initial seed from system timer (not truly random but better than nothing for early boot)
    s[0] = timer_get_ticks();
    s[1] = timer_get_ticks() ^ 0xDEADBEEFCAFEUL; // Mix with a constant

    // Add some initial entropy if possible (e.g., more timer reads, other system values)

    random_add_entropy(timer_get_ticks() ^ 0x123456789ABCDEF0UL);

    initialized = true;
    klog(LOG_INFO, "CSPRNG: Initialized with initial seed.");
}

void random_add_entropy(uint64_t entropy_value) {
    if (!initialized) {
        // If not yet initialized, just use as part of initial seed
        s[0] ^= entropy_value;
        s[1] ^= (entropy_value >> 32) | (entropy_value << 32);
        return;
    }

    // Mix new entropy into the state using a simple XOR.
    // A stronger CSPRNG would use a cryptographic hash or cipher here.
    s[0] ^= entropy_value;
    s[1] ^= (entropy_value >> 32) | (entropy_value << 32);

    // Simple mixing step for the state
    xorshift128p(); 
    xorshift128p();
    xorshift128p(); // Run a few iterations to mix it well
}

static uint64_t xorshift128p(void) {
    uint64_t s1 = s[0];
    const uint64_t s0 = s[1];
    s[0] = s0;
    s1 ^= s1 << 23; // a
    s[1] = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26); // b, c
    return s[1] + s0;
}

uint64_t random_get_uint64(void) {
    if (!initialized) {
        klog(LOG_WARN, "CSPRNG: Attempted to get random number before initialization. Initializing now (less secure).");
        random_init(); // Emergency initialization, but ideally should be done explicitly
    }
    return xorshift128p();
}

void random_get_bytes(void* buffer, size_t num_bytes) {
    if (!buffer || num_bytes == 0) {
        return;
    }

    uint8_t* byte_buffer = (uint8_t*)buffer;
    for (size_t i = 0; i < num_bytes; i++) {
        byte_buffer[i] = (uint8_t)random_get_uint64(); // Take lower 8 bits
    }
}