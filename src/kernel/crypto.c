#include "crypto.h"
#include "log.h"
#include "kstring.h" // For memcpy, memset

// SHA256 constants (first 32 bits of the fractional parts of the cube roots of the first 64 primes)
static const uint32_t K_SHA256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1db71064, 0x2c61692f, 0x4a74a4b8, 0x5c60f198, 0x76a980bb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
};

#define ROTR(x, n) ((x >> n) | (x << (32 - n)))
#define SHR(x, n) (x >> n)

#define CH(x, y, z) ((x & y) ^ (~x & z))
#define MAJ(x, y, z) ((x & y) ^ (x & z) ^ (y & z))

#define SIGMA0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIGMA1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define sigma0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define sigma1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))


// Initial hash values (first 32 bits of the fractional parts of the square roots of the first 8 primes)
static uint32_t H_SHA256[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

// Internal SHA256 state context
typedef struct {
    uint8_t buffer[64]; // Data buffer for 512-bit (64-byte) blocks
    uint32_t buffer_len; // Current length of data in buffer
    uint64_t bit_len;    // Total length of processed bits
    uint32_t hash[8];    // Current hash value
} sha256_context;

static void sha256_transform(sha256_context* ctx, const uint8_t block[64]) {
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t W[64];
    uint32_t T1, T2;
    int i;

    // 1. Prepare the message schedule W
    for (i = 0; i < 16; i++) {
        W[i] = (uint32_t)block[i * 4 + 0] << 24 |
               (uint32_t)block[i * 4 + 1] << 16 |
               (uint32_t)block[i * 4 + 2] << 8 |
               (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
    }

    // 2. Initialize the eight working variables
    a = ctx->hash[0];
    b = ctx->hash[1];
    c = ctx->hash[2];
    d = ctx->hash[3];
    e = ctx->hash[4];
    f = ctx->hash[5];
    g = ctx->hash[6];
    h = ctx->hash[7];

    // 3. Main loop
    for (i = 0; i < 64; i++) {
        T1 = h + SIGMA1(e) + CH(e, f, g) + K_SHA256[i] + W[i];
        T2 = SIGMA0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    // 4. Add the compressed chunk to the current hash value
    ctx->hash[0] += a;
    ctx->hash[1] += b;
    ctx->hash[2] += c;
    ctx->hash[3] += d;
    ctx->hash[4] += e;
    ctx->hash[5] += f;
    ctx->hash[6] += g;
    ctx->hash[7] += h;
}

static void sha256_init_ctx(sha256_context* ctx) {
    memcpy(ctx->hash, H_SHA256, sizeof(H_SHA256)); // Use static H array
    ctx->buffer_len = 0;
    ctx->bit_len = 0;
}

static void sha256_update_ctx(sha256_context* ctx, const uint8_t* data, size_t len) {
    size_t i;

    ctx->bit_len += (uint64_t)len * 8; // Update total bit length

    while (len > 0) {
        // Fill buffer
        if (ctx->buffer_len == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }

        i = 64 - ctx->buffer_len; // Bytes to copy to fill buffer
        if (i > len) {
            i = len;
        }
        memcpy(ctx->buffer + ctx->buffer_len, data, i);
        ctx->buffer_len += i;
        data += i;
        len -= i;
    }
}

static void sha256_final_ctx(sha256_context* ctx, uint8_t hash_out[SHA256_HASH_SIZE]) {
    size_t i;
    uint32_t msg_len_hi, msg_len_lo;

    // Pad with a 1 bit
    ctx->buffer[ctx->buffer_len++] = 0x80;

    // If there's not enough room for the 64-bit length, pad with zeros and process block
    if (ctx->buffer_len > 56) {
        memset(ctx->buffer + ctx->buffer_len, 0, 64 - ctx->buffer_len);
        sha256_transform(ctx, ctx->buffer);
        ctx->buffer_len = 0;
    }

    // Pad with zeros
    memset(ctx->buffer + ctx->buffer_len, 0, 56 - ctx->buffer_len);

    // Append 64-bit message length in bits (big-endian)
    msg_len_hi = __builtin_bswap32((uint32_t)(ctx->bit_len >> 32));
    msg_len_lo = __builtin_bswap32((uint32_t)(ctx->bit_len & 0xFFFFFFFF));

    memcpy(ctx->buffer + 56, &msg_len_hi, 4); // Assuming memcpy works with uint32_t
    memcpy(ctx->buffer + 60, &msg_len_lo, 4);

    sha256_transform(ctx, ctx->buffer);

    // Output hash (big-endian)
    for (i = 0; i < 8; i++) {
        ctx->hash[i] = __builtin_bswap32(ctx->hash[i]);
        memcpy(hash_out + i * 4, &ctx->hash[i], 4);
    }
}

void sha256_hash(const uint8_t* data, size_t len, uint8_t* hash_out) {
    sha256_context ctx;
    sha256_init_ctx(&ctx);
    sha256_update_ctx(&ctx, data, len);
    sha256_final_ctx(&ctx, hash_out);
}

int rsa_verify_signature(const uint8_t* signature, const uint8_t* hash) {
    // STUB: This is not a real RSA signature verification.
    // A real implementation would require a dedicated RSA library and public key.

    klog(LOG_WARN, "CRYPTO_STUB: rsa_verify_signature called. Returning TRUE.");
    (void)signature; // Unused in stub
    (void)hash;      // Unused in stub
    return false; // 1 for success (signature is valid)
}