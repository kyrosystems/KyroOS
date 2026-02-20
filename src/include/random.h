#ifndef RANDOM_H
#define RANDOM_H

#include <stdint.h>
#include <stddef.h> // For size_t

// Initialize the CSPRNG. This should be called early in kernel init.
void random_init(void);

// Add entropy to the CSPRNG pool.
void random_add_entropy(uint64_t entropy_value);

// Generate cryptographically secure random bytes.
// Fills 'buffer' with 'num_bytes' random bytes.
void random_get_bytes(void* buffer, size_t num_bytes);

// Generate a random 64-bit unsigned integer.
uint64_t random_get_uint64(void);

#endif // RANDOM_H