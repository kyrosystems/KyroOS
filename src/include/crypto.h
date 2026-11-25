#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_HASH_SIZE 32

// STUB: A real implementation would have a context structure.
void sha256_hash(const uint8_t* data, size_t len, uint8_t* hash_out);

// STUB: Verifies an RSA signature against a SHA256 hash.
// A real implementation would take a public key.
int rsa_verify_signature(const uint8_t* signature, const uint8_t* hash);

#endif // CRYPTO_H
