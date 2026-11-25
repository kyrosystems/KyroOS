#include "libtls.h"
#include "kyroolib.h"

/* ============================================================================
 * Incremental SHA-256 (FIPS 180-4) for TLS 1.3 transcript & HKDF
 * ============================================================================ */
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buf[64];
    size_t buflen;
} tls_sha256_t;

static const uint32_t sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static inline uint32_t rotr32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

static void sha256_transform(tls_sha256_t *ctx, const uint8_t blk[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) |
               ((uint32_t)blk[i * 4 + 2] << 8) | (uint32_t)blk[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t tmp1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t tmp2 = S0 + maj;
        h = g; g = f; f = e; e = d + tmp1;
        d = c; c = b; b = a; a = tmp1 + tmp2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(tls_sha256_t *ctx) {
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->bitlen = 0;
    ctx->buflen = 0;
}

static void sha256_update(tls_sha256_t *ctx, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        ctx->buf[ctx->buflen++] = p[i];
        if (ctx->buflen == 64) {
            sha256_transform(ctx, ctx->buf);
            ctx->bitlen += 512;
            ctx->buflen = 0;
        }
    }
}

static void sha256_final(tls_sha256_t *ctx, uint8_t out[32]) {
    size_t i = ctx->buflen;
    ctx->bitlen += (uint64_t)ctx->buflen * 8u;
    ctx->buf[i++] = 0x80;
    if (i > 56) {
        while (i < 64) ctx->buf[i++] = 0;
        sha256_transform(ctx, ctx->buf);
        i = 0;
    }
    while (i < 56) ctx->buf[i++] = 0;
    for (int j = 0; j < 8; j++) {
        ctx->buf[56 + j] = (uint8_t)(ctx->bitlen >> ((7 - j) * 8));
    }
    sha256_transform(ctx, ctx->buf);
    for (int j = 0; j < 8; j++) {
        out[j * 4 + 0] = (uint8_t)(ctx->state[j] >> 24);
        out[j * 4 + 1] = (uint8_t)(ctx->state[j] >> 16);
        out[j * 4 + 2] = (uint8_t)(ctx->state[j] >> 8);
        out[j * 4 + 3] = (uint8_t)(ctx->state[j]);
    }
}

static void sha256_oneshot(const void *data, size_t len, uint8_t out[32]) {
    tls_sha256_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

static void sha256_snapshot(const tls_sha256_t *ctx, uint8_t out[32]) {
    tls_sha256_t copy = *ctx;
    sha256_final(&copy, out);
}

static void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32]) {
    uint8_t k_pad[64];
    memset(k_pad, 0, sizeof(k_pad));
    if (klen > 64) sha256_oneshot(key, klen, k_pad);
    else memcpy(k_pad, key, klen);

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k_pad[i] ^ 0x36u;
        opad[i] = k_pad[i] ^ 0x5cu;
    }
    uint8_t inner[32];
    tls_sha256_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, msg, mlen);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

static void hkdf_extract(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t prk[32]) {
    uint8_t zeros[32];
    if (!salt || salt_len == 0) {
        memset(zeros, 0, sizeof(zeros));
        salt = zeros;
        salt_len = 32;
    }
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

static void hkdf_expand_label(const uint8_t secret[32], const char *label,
                              const uint8_t *context, size_t ctx_len,
                              uint8_t *out, size_t out_len) {
    uint8_t info[512];
    size_t lbl_len = strlen(label);
    size_t full_lbl = 6 + lbl_len;
    size_t p = 0;
    info[p++] = (uint8_t)(out_len >> 8);
    info[p++] = (uint8_t)(out_len & 0xFF);
    info[p++] = (uint8_t)full_lbl;
    memcpy(info + p, "tls13 ", 6); p += 6;
    memcpy(info + p, label, lbl_len); p += lbl_len;
    info[p++] = (uint8_t)ctx_len;
    if (ctx_len > 0) {
        memcpy(info + p, context, ctx_len);
        p += ctx_len;
    }

    uint8_t t[32];
    size_t t_len = 0;
    size_t generated = 0;
    uint8_t counter = 1;
    while (generated < out_len) {
        uint8_t msg[32 + 512 + 1];
        if (t_len > 0) memcpy(msg, t, t_len);
        memcpy(msg + t_len, info, p);
        msg[t_len + p] = counter++;
        hmac_sha256(secret, 32, msg, t_len + p + 1, t);
        t_len = 32;
        size_t copy = out_len - generated;
        if (copy > 32) copy = 32;
        memcpy(out + generated, t, copy);
        generated += copy;
    }
}

static void derive_secret(const uint8_t secret[32], const char *label, const uint8_t transcript_hash[32], uint8_t out[32]) {
    hkdf_expand_label(secret, label, transcript_hash, 32, out, 32);
}

/* ============================================================================
 * RFC 7748 X25519 (TweetNaCl constant-time Curve25519 ladder)
 * ============================================================================ */
typedef int64_t gf[16];
static const gf _121665 = {0xDB41, 1};

static void gf_car(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void gf_sel(gf p, gf q, int b) {
    int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void gf_pack(uint8_t o[32], const gf n) {
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    gf_car(t);
    gf_car(t);
    gf_car(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        gf_sel(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void gf_unpack(gf o, const uint8_t n[32]) {
    for (int i = 0; i < 16; i++) {
        o[i] = (int64_t)n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    }
    o[15] &= 0x7fff;
}

static void gf_add(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

static void gf_sub(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

static void gf_mul(gf o, const gf a, const gf b) {
    int64_t t[31] = {0};
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    gf_car(o);
    gf_car(o);
}

static void gf_sq(gf o, const gf a) {
    gf_mul(o, a, a);
}

static void gf_inv(gf o, const gf i) {
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        gf_sq(c, c);
        if (a != 2 && a != 4) gf_mul(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

static void x25519(uint8_t q[32], const uint8_t n[32], const uint8_t p[32]) {
    uint8_t z[32];
    memcpy(z, n, 32);
    z[0] &= 248;
    z[31] &= 127;
    z[31] |= 64;

    gf x, a, b, c, d, e, f;
    gf_unpack(x, p);
    for (int i = 0; i < 16; i++) {
        b[i] = x[i];
        a[i] = c[i] = d[i] = 0;
    }
    a[0] = 1;
    d[0] = 1;

    for (int i = 254; i >= 0; i--) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        gf_sel(a, b, r);
        gf_sel(c, d, r);
        gf_add(e, a, c);
        gf_sub(a, a, c);
        gf_add(c, b, d);
        gf_sub(b, b, d);
        gf_sq(d, e);
        gf_sq(f, a);
        gf_mul(a, c, a);
        gf_mul(c, b, e);
        gf_add(e, a, c);
        gf_sub(a, a, c);
        gf_sq(b, a);
        gf_sub(c, d, f);
        gf_mul(a, c, _121665);
        gf_add(a, a, d);
        gf_mul(c, c, a);
        gf_mul(a, d, f);
        gf_mul(d, b, x);
        gf_sq(b, e);
        gf_sel(a, b, r);
        gf_sel(c, d, r);
    }
    gf_inv(c, c);
    gf_mul(a, a, c);
    gf_pack(q, a);
}

/* ============================================================================
 * ChaCha20-Poly1305 AEAD (RFC 8439)
 * ============================================================================ */
static inline uint32_t load32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void store32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline uint32_t rotl32(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32u - n));
}

#define QR(a, b, c, d) do { \
    a += b; d = rotl32(d ^ a, 16); \
    c += d; b = rotl32(b ^ c, 12); \
    a += b; d = rotl32(d ^ a, 8);  \
    c += d; b = rotl32(b ^ c, 7);  \
} while (0)

static void chacha20_block(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12], uint8_t out[64]) {
    uint32_t s[16];
    s[0] = 0x61707865u; s[1] = 0x3320646eu; s[2] = 0x79622d32u; s[3] = 0x6b206574u;
    for (int i = 0; i < 8; i++) s[4 + i] = load32_le(key + i * 4);
    s[12] = counter;
    s[13] = load32_le(nonce);
    s[14] = load32_le(nonce + 4);
    s[15] = load32_le(nonce + 8);

    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = s[i];
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8],  x[12]);
        QR(x[1], x[5], x[9],  x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8],  x[13]);
        QR(x[3], x[4], x[9],  x[14]);
    }
    for (int i = 0; i < 16; i++) store32_le(out + i * 4, x[i] + s[i]);
}

static void chacha20_xor(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12],
                         const uint8_t *in, uint8_t *out, size_t len) {
    uint8_t block[64];
    size_t off = 0;
    while (off < len) {
        chacha20_block(key, counter++, nonce, block);
        size_t chunk = len - off;
        if (chunk > 64) chunk = 64;
        for (size_t i = 0; i < chunk; i++) out[off + i] = in[off + i] ^ block[i];
        off += chunk;
    }
}

typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    uint8_t buf[16];
    size_t leftover;
} poly1305_ctx_t;

static void poly1305_init(poly1305_ctx_t *st, const uint8_t key[32]) {
    st->r[0] = (load32_le(key + 0)) & 0x3ffffffu;
    st->r[1] = (load32_le(key + 3) >> 2) & 0x3ffff03u;
    st->r[2] = (load32_le(key + 6) >> 4) & 0x3ffc0ffu;
    st->r[3] = (load32_le(key + 9) >> 6) & 0x3f03fffu;
    st->r[4] = (load32_le(key + 12) >> 8) & 0x00fffffu;
    for (int i = 0; i < 5; i++) st->h[i] = 0;
    for (int i = 0; i < 4; i++) st->pad[i] = load32_le(key + 16 + i * 4);
    st->leftover = 0;
}

static void poly1305_blocks(poly1305_ctx_t *st, const uint8_t *m, size_t bytes, int is_final) {
    const uint32_t hibit = is_final ? 0u : (1u << 24);
    uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
    uint32_t s1 = r1 * 5u, s2 = r2 * 5u, s3 = r3 * 5u, s4 = r4 * 5u;
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];

    while (bytes >= 16) {
        h0 += (load32_le(m + 0)) & 0x3ffffffu;
        h1 += (load32_le(m + 3) >> 2) & 0x3ffffffu;
        h2 += (load32_le(m + 6) >> 4) & 0x3ffffffu;
        h3 += (load32_le(m + 9) >> 6) & 0x3ffffffu;
        h4 += (load32_le(m + 12) >> 8) | hibit;

        uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

        uint32_t c;
        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffffu; d1 += c;
        c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffffu; d2 += c;
        c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffffu; d3 += c;
        c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffffu; d4 += c;
        c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffffu; h0 += c * 5u;
        c = (h0 >> 26);            h0 &= 0x3ffffffu;               h1 += c;

        m += 16;
        bytes -= 16;
    }
    st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
}

static void poly1305_update(poly1305_ctx_t *st, const uint8_t *m, size_t bytes) {
    if (st->leftover) {
        size_t want = 16 - st->leftover;
        if (want > bytes) want = bytes;
        memcpy(st->buf + st->leftover, m, want);
        bytes -= want;
        m += want;
        st->leftover += want;
        if (st->leftover < 16) return;
        poly1305_blocks(st, st->buf, 16, 0);
        st->leftover = 0;
    }
    if (bytes >= 16) {
        size_t want = bytes & ~(size_t)15;
        poly1305_blocks(st, m, want, 0);
        m += want;
        bytes -= want;
    }
    if (bytes) {
        memcpy(st->buf + st->leftover, m, bytes);
        st->leftover += bytes;
    }
}

static void poly1305_finish(poly1305_ctx_t *st, uint8_t mac[16]) {
    if (st->leftover) {
        size_t i = st->leftover;
        st->buf[i++] = 1;
        while (i < 16) st->buf[i++] = 0;
        poly1305_blocks(st, st->buf, 16, 1);
    }
    uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];
    uint32_t c;
    c = h1 >> 26; h1 &= 0x3ffffffu; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffffu; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffffu; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffffu; h0 += c * 5u;
    c = h0 >> 26; h0 &= 0x3ffffffu; h1 += c;

    uint32_t g0 = h0 + 5u; c = g0 >> 26; g0 &= 0x3ffffffu;
    uint32_t g1 = h1 + c;  c = g1 >> 26; g1 &= 0x3ffffffu;
    uint32_t g2 = h2 + c;  c = g2 >> 26; g2 &= 0x3ffffffu;
    uint32_t g3 = h3 + c;  c = g3 >> 26; g3 &= 0x3ffffffu;
    uint32_t g4 = h4 + c - (1u << 26);

    uint32_t mask = (g4 >> 31) - 1u;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

    h0 = (h0 | (h1 << 26)) & 0xffffffffu;
    h1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffffu;
    h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffffu;
    h3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffffu;

    uint64_t f;
    f = (uint64_t)h0 + st->pad[0];              h0 = (uint32_t)f;
    f = (uint64_t)h1 + st->pad[1] + (f >> 32);  h1 = (uint32_t)f;
    f = (uint64_t)h2 + st->pad[2] + (f >> 32);  h2 = (uint32_t)f;
    f = (uint64_t)h3 + st->pad[3] + (f >> 32);  h3 = (uint32_t)f;

    store32_le(mac + 0, h0);
    store32_le(mac + 4, h1);
    store32_le(mac + 8, h2);
    store32_le(mac + 12, h3);
}

static void poly1305_aead_tag(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *ct, size_t ct_len, uint8_t tag[16]) {
    uint8_t otk[64];
    chacha20_block(key, 0, nonce, otk);
    poly1305_ctx_t p;
    poly1305_init(&p, otk);
    uint8_t zeros[16];
    memset(zeros, 0, sizeof(zeros));
    poly1305_update(&p, aad, aad_len);
    if (aad_len % 16) poly1305_update(&p, zeros, 16 - (aad_len % 16));
    poly1305_update(&p, ct, ct_len);
    if (ct_len % 16) poly1305_update(&p, zeros, 16 - (ct_len % 16));
    uint8_t lens[16];
    store32_le(lens + 0, (uint32_t)aad_len);
    store32_le(lens + 4, (uint32_t)((uint64_t)aad_len >> 32));
    store32_le(lens + 8, (uint32_t)ct_len);
    store32_le(lens + 12, (uint32_t)((uint64_t)ct_len >> 32));
    poly1305_update(&p, lens, 16);
    poly1305_finish(&p, tag);
}

/* ============================================================================
 * AES-128-GCM AEAD (NIST SP 800-38D)
 * ============================================================================ */
static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static inline uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1u) * 0x1bu));
}

static void aes128_expand_key(const uint8_t key[16], uint8_t rk[176]) {
    static const uint8_t rcon[11] = {0, 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
    memcpy(rk, key, 16);
    int i = 4;
    uint8_t tmp[4];
    while (i < 44) {
        memcpy(tmp, rk + (i - 1) * 4, 4);
        if (i % 4 == 0) {
            uint8_t t = tmp[0];
            tmp[0] = aes_sbox[tmp[1]] ^ rcon[i / 4];
            tmp[1] = aes_sbox[tmp[2]];
            tmp[2] = aes_sbox[tmp[3]];
            tmp[3] = aes_sbox[t];
        }
        for (int j = 0; j < 4; j++) rk[i * 4 + j] = rk[(i - 4) * 4 + j] ^ tmp[j];
        i++;
    }
}

static void aes128_encrypt_block(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for (int r = 1; r <= 10; r++) {
        for (int i = 0; i < 16; i++) s[i] = aes_sbox[s[i]];
        uint8_t t;
        t = s[1]; s[1] = s[5]; s[5] = s[9];  s[9]  = s[13]; s[13] = t;
        t = s[2]; s[2] = s[10]; s[10] = t;   t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
        if (r < 10) {
            for (int c = 0; c < 4; c++) {
                uint8_t *col = s + c * 4;
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                uint8_t xr = a0 ^ a1 ^ a2 ^ a3;
                col[0] ^= xr ^ xtime(a0 ^ a1);
                col[1] ^= xr ^ xtime(a1 ^ a2);
                col[2] ^= xr ^ xtime(a2 ^ a3);
                col[3] ^= xr ^ xtime(a3 ^ a0);
            }
        }
        const uint8_t *rkp = rk + r * 16;
        for (int i = 0; i < 16; i++) s[i] ^= rkp[i];
    }
    memcpy(out, s, 16);
}

static void gf128_mul(const uint8_t x[16], const uint8_t y[16], uint8_t z[16]) {
    uint8_t v[16];
    memset(z, 0, 16);
    memcpy(v, y, 16);
    for (int i = 0; i < 128; i++) {
        if ((x[i >> 3] >> (7 - (i & 7))) & 1u) {
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        }
        uint8_t lsb = v[15] & 1u;
        for (int j = 15; j > 0; j--) v[j] = (uint8_t)((v[j] >> 1) | ((v[j - 1] & 1u) << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1u;
    }
}

static void ghash_update(const uint8_t h[16], uint8_t acc[16], const uint8_t *data, size_t len) {
    size_t off = 0;
    uint8_t blk[16], tmp[16];
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 16) chunk = 16;
        memset(blk, 0, 16);
        memcpy(blk, data + off, chunk);
        for (int i = 0; i < 16; i++) blk[i] ^= acc[i];
        gf128_mul(blk, h, tmp);
        memcpy(acc, tmp, 16);
        off += chunk;
    }
}

static void aes_gcm_crypt(const uint8_t key[16], const uint8_t nonce[12],
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *in, uint8_t *out, size_t len,
                          uint8_t tag[16], bool encrypt) {
    uint8_t rk[176], h[16], j0[16], ctr[16], e0[16], keystream[16];
    uint8_t zero16[16];
    memset(zero16, 0, sizeof(zero16));
    aes128_expand_key(key, rk);
    aes128_encrypt_block(rk, zero16, h);

    memcpy(j0, nonce, 12);
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    memcpy(ctr, j0, 16);

    for (size_t off = 0; off < len; off += 16) {
        uint32_t cval = ((uint32_t)ctr[12] << 24) | ((uint32_t)ctr[13] << 16) |
                        ((uint32_t)ctr[14] << 8) | (uint32_t)ctr[15];
        cval++;
        ctr[12] = (uint8_t)(cval >> 24); ctr[13] = (uint8_t)(cval >> 16);
        ctr[14] = (uint8_t)(cval >> 8);  ctr[15] = (uint8_t)cval;
        aes128_encrypt_block(rk, ctr, keystream);
        size_t chunk = len - off;
        if (chunk > 16) chunk = 16;
        for (size_t i = 0; i < chunk; i++) out[off + i] = in[off + i] ^ keystream[i];
    }

    uint8_t gacc[16];
    memset(gacc, 0, sizeof(gacc));
    ghash_update(h, gacc, aad, aad_len);
    const uint8_t *ct = encrypt ? out : in;
    ghash_update(h, gacc, ct, len);

    uint8_t lens[16];
    uint64_t abits = (uint64_t)aad_len * 8u;
    uint64_t cbits = (uint64_t)len * 8u;
    for (int i = 0; i < 8; i++) {
        lens[i] = (uint8_t)(abits >> ((7 - i) * 8));
        lens[8 + i] = (uint8_t)(cbits >> ((7 - i) * 8));
    }
    ghash_update(h, gacc, lens, 16);

    aes128_encrypt_block(rk, j0, e0);
    for (int i = 0; i < 16; i++) tag[i] = gacc[i] ^ e0[i];
}

/* ============================================================================
 * Randomness & Socket Helpers
 * ============================================================================ */
static void fill_random(uint8_t *buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        int r = read(fd, buf, len);
        close(fd);
        if (r == (int)len) return;
    }
    uint64_t seed = get_ticks() ^ 0x9e3779b97f4a7c15ULL;
    uint32_t lo = 0, hi = 0;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    seed ^= ((uint64_t)hi << 32) | lo;
    for (size_t i = 0; i < len; i++) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        buf[i] = (uint8_t)(seed >> 32);
    }
}

static int sock_read_exact(int fd, uint8_t *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int n = read(fd, buf + got, len - got);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static int sock_write_all(int fd, const uint8_t *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = write(fd, buf + sent, len - sent);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* ============================================================================
 * TLS 1.3 Record Encryption / Decryption
 * ============================================================================ */
static void make_nonce(const uint8_t iv[12], uint64_t seq, uint8_t out[12]) {
    memcpy(out, iv, 12);
    for (int i = 0; i < 8; i++) {
        out[4 + i] ^= (uint8_t)(seq >> ((7 - i) * 8));
    }
}

static int tls_aead_encrypt_record(uint16_t suite, const uint8_t *key, const uint8_t iv[12], uint64_t *seq,
                                   uint8_t inner_ctype, const uint8_t *pt, size_t pt_len,
                                   uint8_t *rec_out, size_t *rec_len_out) {
    size_t inner_len = pt_len + 1;
    size_t ct_len = inner_len + 16;
    if (ct_len > 18432) return -1;

    uint8_t *work = malloc(inner_len);
    if (!work) return -1;
    if (pt_len > 0) memcpy(work, pt, pt_len);
    work[pt_len] = inner_ctype;

    rec_out[0] = 0x17;
    rec_out[1] = 0x03;
    rec_out[2] = 0x03;
    rec_out[3] = (uint8_t)(ct_len >> 8);
    rec_out[4] = (uint8_t)(ct_len & 0xFF);

    uint8_t nonce[12];
    make_nonce(iv, *seq, nonce);
    (*seq)++;

    uint8_t *ct = rec_out + 5;
    uint8_t *tag = ct + inner_len;
    if (suite == TLS_CIPHER_CHACHA20_POLY1305_SHA256) {
        chacha20_xor(key, 1, nonce, work, ct, inner_len);
        poly1305_aead_tag(key, nonce, rec_out, 5, ct, inner_len, tag);
    } else {
        aes_gcm_crypt(key, nonce, rec_out, 5, work, ct, inner_len, tag, true);
    }
    free(work);
    *rec_len_out = 5 + ct_len;
    return 0;
}

static int tls_aead_decrypt_record(uint16_t suite, const uint8_t *key, const uint8_t iv[12], uint64_t *seq,
                                   const uint8_t hdr[5], const uint8_t *payload, size_t payload_len,
                                   uint8_t *pt_out, size_t *pt_len_out, uint8_t *inner_ctype) {
    if (payload_len < 17) return -1;
    size_t inner_len = payload_len - 16;
    const uint8_t *rx_tag = payload + inner_len;

    uint8_t nonce[12];
    make_nonce(iv, *seq, nonce);
    (*seq)++;

    uint8_t calc_tag[16];
    if (suite == TLS_CIPHER_CHACHA20_POLY1305_SHA256) {
        poly1305_aead_tag(key, nonce, hdr, 5, payload, inner_len, calc_tag);
        uint8_t diff = 0;
        for (int i = 0; i < 16; i++) diff |= rx_tag[i] ^ calc_tag[i];
        if (diff != 0) return -1;
        chacha20_xor(key, 1, nonce, payload, pt_out, inner_len);
    } else {
        aes_gcm_crypt(key, nonce, hdr, 5, payload, pt_out, inner_len, calc_tag, false);
        uint8_t diff = 0;
        for (int i = 0; i < 16; i++) diff |= rx_tag[i] ^ calc_tag[i];
        if (diff != 0) return -1;
    }

    while (inner_len > 0 && pt_out[inner_len - 1] == 0) inner_len--;
    if (inner_len == 0) return -1;
    *inner_ctype = pt_out[inner_len - 1];
    *pt_len_out = inner_len - 1;
    return 0;
}

/* ============================================================================
 * Public TLS Session API
 * ============================================================================ */
tls_session_t *tls_create_session(int sockfd, const char *hostname) {
    tls_session_t *s = malloc(sizeof(tls_session_t));
    if (!s) return NULL;
    memset(s, 0, sizeof(tls_session_t));
    s->sockfd = sockfd;
    strncpy(s->hostname, hostname, sizeof(s->hostname) - 1);

    fill_random(s->client_random, 32);
    fill_random(s->client_private_key, 32);

    static const uint8_t basepoint[32] = {9};
    x25519(s->client_public_key, s->client_private_key, basepoint);
    return s;
}

static int tls_read_record_raw(int fd, uint8_t hdr[5], uint8_t *buf, size_t max_len, uint16_t *rlen_out) {
    for (;;) {
        if (sock_read_exact(fd, hdr, 5) != 0) return -1;
        uint16_t rlen = ((uint16_t)hdr[3] << 8) | hdr[4];
        if (rlen > max_len) return -1;
        if (rlen > 0 && sock_read_exact(fd, buf, rlen) != 0) return -1;
        /* Ignore legacy ChangeCipherSpec records emitted by TLS 1.3 middlebox compat */
        if (hdr[0] == 0x14) continue;
        *rlen_out = rlen;
        return 0;
    }
}

int tls_handshake(tls_session_t *tls) {
    if (!tls) return -1;

    uint8_t *rec_buf = malloc(18432);
    uint8_t *pt_buf  = malloc(18432);
    uint8_t *hs_buf  = malloc(32768);
    if (!rec_buf || !pt_buf || !hs_buf) {
        if (rec_buf) free(rec_buf);
        if (pt_buf)  free(pt_buf);
        if (hs_buf)  free(hs_buf);
        return -1;
    }

    tls_sha256_t transcript;
    sha256_init(&transcript);

    /* ---- Build ClientHello ---- */
    uint8_t *ch = rec_buf;
    int p = 0;
    ch[p++] = 0x16; ch[p++] = 0x03; ch[p++] = 0x01; // Record header (legacy 0x0301 for CH)
    int rec_len_idx = p; p += 2;

    int hs_start = p;
    ch[p++] = 0x01; // ClientHello
    int hs_len_idx = p; p += 3;

    ch[p++] = 0x03; ch[p++] = 0x03; // legacy_version = TLS 1.2
    memcpy(ch + p, tls->client_random, 32); p += 32;

    ch[p++] = 32; // legacy_session_id (32 bytes for middlebox compatibility)
    fill_random(ch + p, 32);
    p += 32;

    // Cipher suites: TLS_CHACHA20_POLY1305_SHA256 (0x1303), TLS_AES_128_GCM_SHA256 (0x1301)
    ch[p++] = 0x00; ch[p++] = 0x04;
    ch[p++] = 0x13; ch[p++] = 0x03;
    ch[p++] = 0x13; ch[p++] = 0x01;

    ch[p++] = 0x01; ch[p++] = 0x00; // compression methods: null

    int ext_len_idx = p; p += 2;
    int ext_start = p;

    // 1. SNI Extension (0x0000)
    int host_len = (int)strlen(tls->hostname);
    if (host_len > 0 && host_len < 250) {
        ch[p++] = 0x00; ch[p++] = 0x00;
        ch[p++] = (uint8_t)((host_len + 5) >> 8); ch[p++] = (uint8_t)(host_len + 5);
        ch[p++] = (uint8_t)((host_len + 3) >> 8); ch[p++] = (uint8_t)(host_len + 3);
        ch[p++] = 0x00;
        ch[p++] = (uint8_t)(host_len >> 8); ch[p++] = (uint8_t)(host_len & 0xFF);
        memcpy(ch + p, tls->hostname, host_len); p += host_len;
    }

    // 2. Supported Versions (0x002B): TLS 1.3 (0x0304)
    ch[p++] = 0x00; ch[p++] = 0x2B;
    ch[p++] = 0x00; ch[p++] = 0x03;
    ch[p++] = 0x02; ch[p++] = 0x03; ch[p++] = 0x04;

    // 3. Supported Groups (0x000A): X25519 (0x001D)
    ch[p++] = 0x00; ch[p++] = 0x0A;
    ch[p++] = 0x00; ch[p++] = 0x04;
    ch[p++] = 0x00; ch[p++] = 0x02;
    ch[p++] = 0x00; ch[p++] = 0x1D;

    // 4. Key Share (0x0033): X25519
    ch[p++] = 0x00; ch[p++] = 0x33;
    ch[p++] = 0x00; ch[p++] = 38;
    ch[p++] = 0x00; ch[p++] = 36;
    ch[p++] = 0x00; ch[p++] = 0x1D;
    ch[p++] = 0x00; ch[p++] = 32;
    memcpy(ch + p, tls->client_public_key, 32); p += 32;

    // 5. Signature Algorithms (0x000D)
    ch[p++] = 0x00; ch[p++] = 0x0D;
    ch[p++] = 0x00; ch[p++] = 0x0E;
    ch[p++] = 0x00; ch[p++] = 0x0C;
    ch[p++] = 0x08; ch[p++] = 0x04; // rsa_pss_rsae_sha256
    ch[p++] = 0x08; ch[p++] = 0x05; // rsa_pss_rsae_sha384
    ch[p++] = 0x08; ch[p++] = 0x06; // rsa_pss_rsae_sha512
    ch[p++] = 0x04; ch[p++] = 0x03; // ecdsa_secp256r1_sha256
    ch[p++] = 0x08; ch[p++] = 0x07; // ed25519
    ch[p++] = 0x04; ch[p++] = 0x01; // rsa_pkcs1_sha256

    // 6. PSK Key Exchange Modes (0x002D)
    ch[p++] = 0x00; ch[p++] = 0x2D;
    ch[p++] = 0x00; ch[p++] = 0x02;
    ch[p++] = 0x01; ch[p++] = 0x01;

    int ext_len = p - ext_start;
    ch[ext_len_idx]     = (uint8_t)(ext_len >> 8);
    ch[ext_len_idx + 1] = (uint8_t)(ext_len & 0xFF);

    int hs_len = p - (hs_start + 4);
    ch[hs_len_idx]     = (uint8_t)(hs_len >> 16);
    ch[hs_len_idx + 1] = (uint8_t)(hs_len >> 8);
    ch[hs_len_idx + 2] = (uint8_t)(hs_len & 0xFF);

    int rec_len = p - 5;
    ch[rec_len_idx]     = (uint8_t)(rec_len >> 8);
    ch[rec_len_idx + 1] = (uint8_t)(rec_len & 0xFF);

    sha256_update(&transcript, ch + hs_start, (size_t)(p - hs_start));

    if (sock_write_all(tls->sockfd, ch, (size_t)p) != 0) goto fail;

    /* ---- Read ServerHello ---- */
    uint8_t hdr[5];
    uint16_t rlen = 0;
    if (tls_read_record_raw(tls->sockfd, hdr, rec_buf, 18432, &rlen) != 0) goto fail;
    if (hdr[0] != 0x16 || rlen < 42 || rec_buf[0] != 0x02) goto fail;

    uint32_t sh_len = ((uint32_t)rec_buf[1] << 16) | ((uint32_t)rec_buf[2] << 8) | rec_buf[3];
    if (4u + sh_len > rlen) goto fail;
    sha256_update(&transcript, rec_buf, 4u + sh_len);

    size_t sp = 4;
    sp += 2; // legacy version
    memcpy(tls->server_random, rec_buf + sp, 32); sp += 32;
    uint8_t sess_id_len = rec_buf[sp++];
    if (sp + sess_id_len + 3 > 4u + sh_len) goto fail;
    sp += sess_id_len;

    tls->cipher_suite = ((uint16_t)rec_buf[sp] << 8) | rec_buf[sp + 1];
    sp += 2;
    if (tls->cipher_suite != TLS_CIPHER_CHACHA20_POLY1305_SHA256 &&
        tls->cipher_suite != TLS_CIPHER_AES_128_GCM_SHA256) goto fail;
    sp += 1; // compression method

    bool got_tls13 = false, got_keyshare = false;
    if (sp + 2 <= 4u + sh_len) {
        uint16_t sh_ext_len = ((uint16_t)rec_buf[sp] << 8) | rec_buf[sp + 1];
        sp += 2;
        size_t ext_end = sp + sh_ext_len;
        if (ext_end > 4u + sh_len) goto fail;
        while (sp + 4 <= ext_end) {
            uint16_t etype = ((uint16_t)rec_buf[sp] << 8) | rec_buf[sp + 1];
            uint16_t elen  = ((uint16_t)rec_buf[sp + 2] << 8) | rec_buf[sp + 3];
            sp += 4;
            if (sp + elen > ext_end) goto fail;
            if (etype == 0x002B && elen == 2 && rec_buf[sp] == 0x03 && rec_buf[sp + 1] == 0x04) {
                got_tls13 = true;
            } else if (etype == 0x0033 && elen == 36) {
                uint16_t grp = ((uint16_t)rec_buf[sp] << 8) | rec_buf[sp + 1];
                uint16_t klen = ((uint16_t)rec_buf[sp + 2] << 8) | rec_buf[sp + 3];
                if (grp == 0x001D && klen == 32) {
                    memcpy(tls->server_public_key, rec_buf + sp + 4, 32);
                    got_keyshare = true;
                }
            }
            sp += elen;
        }
    }
    if (!got_tls13 || !got_keyshare) goto fail;

    /* ---- Compute Handshake Traffic Secrets & Keys ---- */
    x25519(tls->shared_secret, tls->client_private_key, tls->server_public_key);

    uint8_t zeros32[32], empty_hash[32];
    memset(zeros32, 0, 32);
    sha256_oneshot("", 0, empty_hash);

    uint8_t early_secret[32], derived1[32], handshake_secret[32];
    hkdf_extract(zeros32, 32, zeros32, 32, early_secret);
    derive_secret(early_secret, "derived", empty_hash, derived1);
    hkdf_extract(derived1, 32, tls->shared_secret, 32, handshake_secret);

    uint8_t ch_sh_hash[32];
    sha256_snapshot(&transcript, ch_sh_hash);

    uint8_t c_hs_secret[32], s_hs_secret[32];
    derive_secret(handshake_secret, "c hs traffic", ch_sh_hash, c_hs_secret);
    derive_secret(handshake_secret, "s hs traffic", ch_sh_hash, s_hs_secret);

    size_t key_len = (tls->cipher_suite == TLS_CIPHER_AES_128_GCM_SHA256) ? 16 : 32;
    uint8_t c_hs_key[32], s_hs_key[32], c_hs_iv[12], s_hs_iv[12];
    hkdf_expand_label(c_hs_secret, "key", NULL, 0, c_hs_key, key_len);
    hkdf_expand_label(c_hs_secret, "iv",  NULL, 0, c_hs_iv, 12);
    hkdf_expand_label(s_hs_secret, "key", NULL, 0, s_hs_key, key_len);
    hkdf_expand_label(s_hs_secret, "iv",  NULL, 0, s_hs_iv, 12);

    uint8_t c_fin_key[32], s_fin_key[32];
    hkdf_expand_label(c_hs_secret, "finished", NULL, 0, c_fin_key, 32);
    hkdf_expand_label(s_hs_secret, "finished", NULL, 0, s_fin_key, 32);

    uint64_t c_hs_seq = 0, s_hs_seq = 0;

    /* ---- Read Encrypted Server Handshake Messages until Server Finished ---- */
    size_t hs_len_buf = 0;
    bool got_server_finished = false;
    while (!got_server_finished) {
        if (tls_read_record_raw(tls->sockfd, hdr, rec_buf, 18432, &rlen) != 0) goto fail;
        if (hdr[0] != 0x17) goto fail;

        size_t pt_len = 0;
        uint8_t inner_ctype = 0;
        if (tls_aead_decrypt_record(tls->cipher_suite, s_hs_key, s_hs_iv, &s_hs_seq,
                                    hdr, rec_buf, rlen, pt_buf, &pt_len, &inner_ctype) != 0) {
            goto fail;
        }
        if (inner_ctype == 0x15) goto fail; // Alert
        if (inner_ctype != 0x16) continue;
        if (hs_len_buf + pt_len > 32768) goto fail;
        memcpy(hs_buf + hs_len_buf, pt_buf, pt_len);
        hs_len_buf += pt_len;

        size_t hp = 0;
        while (hp + 4 <= hs_len_buf) {
            uint8_t mtype = hs_buf[hp];
            uint32_t mlen = ((uint32_t)hs_buf[hp + 1] << 16) |
                            ((uint32_t)hs_buf[hp + 2] << 8) | hs_buf[hp + 3];
            if (hp + 4u + mlen > hs_len_buf) break;

            if (mtype == 0x14) { // Server Finished
                if (mlen != 32) goto fail;
                uint8_t cur_hash[32], expected_fin[32];
                sha256_snapshot(&transcript, cur_hash);
                hmac_sha256(s_fin_key, 32, cur_hash, 32, expected_fin);
                uint8_t diff = 0;
                for (int i = 0; i < 32; i++) diff |= expected_fin[i] ^ hs_buf[hp + 4 + i];
                if (diff != 0) goto fail;
                sha256_update(&transcript, hs_buf + hp, 4u + mlen);
                got_server_finished = true;
                hp += 4u + mlen;
                break;
            }
            sha256_update(&transcript, hs_buf + hp, 4u + mlen);
            hp += 4u + mlen;
        }
        if (hp > 0) {
            if (hp < hs_len_buf) memmove(hs_buf, hs_buf + hp, hs_len_buf - hp);
            hs_len_buf -= hp;
        }
    }

    /* ---- Derive Application Traffic Keys BEFORE adding Client Finished to transcript ---- */
    uint8_t fin_hash[32], derived2[32], master_secret[32];
    sha256_snapshot(&transcript, fin_hash);
    derive_secret(handshake_secret, "derived", empty_hash, derived2);
    hkdf_extract(derived2, 32, zeros32, 32, master_secret);

    uint8_t c_app_secret[32], s_app_secret[32];
    derive_secret(master_secret, "c ap traffic", fin_hash, c_app_secret);
    derive_secret(master_secret, "s ap traffic", fin_hash, s_app_secret);

    hkdf_expand_label(c_app_secret, "key", NULL, 0, tls->client_write_key, key_len);
    hkdf_expand_label(c_app_secret, "iv",  NULL, 0, tls->client_write_iv, 12);
    hkdf_expand_label(s_app_secret, "key", NULL, 0, tls->server_write_key, key_len);
    hkdf_expand_label(s_app_secret, "iv",  NULL, 0, tls->server_write_iv, 12);
    tls->client_seq = 0;
    tls->server_seq = 0;

    /* ---- Send Encrypted Client Finished (with Client Handshake Key) ---- */
    uint8_t c_fin_msg[36];
    c_fin_msg[0] = 0x14;
    c_fin_msg[1] = 0x00; c_fin_msg[2] = 0x00; c_fin_msg[3] = 32;
    hmac_sha256(c_fin_key, 32, fin_hash, 32, c_fin_msg + 4);

    size_t cfin_rec_len = 0;
    if (tls_aead_encrypt_record(tls->cipher_suite, c_hs_key, c_hs_iv, &c_hs_seq,
                                0x16, c_fin_msg, 36, rec_buf, &cfin_rec_len) != 0) {
        goto fail;
    }
    if (sock_write_all(tls->sockfd, rec_buf, cfin_rec_len) != 0) goto fail;

    tls->is_established = true;
    free(rec_buf);
    free(pt_buf);
    free(hs_buf);
    return 0;

fail:
    free(rec_buf);
    free(pt_buf);
    free(hs_buf);
    return -1;
}

int tls_send(tls_session_t *tls, const void *buf, size_t len) {
    if (!tls || !tls->is_established || !buf) return -1;
    const uint8_t *p = (const uint8_t *)buf;
    size_t total = 0;
    uint8_t *rec = malloc(18432);
    if (!rec) return -1;

    while (total < len) {
        size_t chunk = len - total;
        if (chunk > 16384) chunk = 16384;
        size_t rec_len = 0;
        if (tls_aead_encrypt_record(tls->cipher_suite, tls->client_write_key, tls->client_write_iv,
                                    &tls->client_seq, 0x17, p + total, chunk, rec, &rec_len) != 0 ||
            sock_write_all(tls->sockfd, rec, rec_len) != 0) {
            free(rec);
            return -1;
        }
        total += chunk;
    }
    free(rec);
    return (int)total;
}

int tls_recv(tls_session_t *tls, void *buf, size_t len) {
    if (!tls || !tls->is_established || !buf || len == 0) return -1;

    if (tls->app_pos < tls->app_len) {
        size_t avail = tls->app_len - tls->app_pos;
        size_t copy = (len < avail) ? len : avail;
        memcpy(buf, tls->app_buf + tls->app_pos, copy);
        tls->app_pos += copy;
        if (tls->app_pos == tls->app_len) {
            tls->app_pos = 0;
            tls->app_len = 0;
        }
        return (int)copy;
    }
    if (tls->eof) return 0;

    uint8_t *rec = malloc(18432);
    if (!rec) return -1;

    for (;;) {
        uint8_t hdr[5];
        uint16_t rlen = 0;
        if (tls_read_record_raw(tls->sockfd, hdr, rec, 18432, &rlen) != 0) {
            free(rec);
            tls->eof = true;
            return 0;
        }
        if (hdr[0] != 0x17) {
            free(rec);
            return -1;
        }
        size_t pt_len = 0;
        uint8_t inner_ctype = 0;
        if (tls_aead_decrypt_record(tls->cipher_suite, tls->server_write_key, tls->server_write_iv,
                                    &tls->server_seq, hdr, rec, rlen,
                                    tls->app_buf, &pt_len, &inner_ctype) != 0) {
            free(rec);
            return -1;
        }
        if (inner_ctype == 0x16) {
            /* Post-handshake message such as NewSessionTicket; skip and read next record */
            continue;
        }
        if (inner_ctype == 0x15) {
            /* TLS Alert */
            tls->eof = true;
            free(rec);
            if (pt_len >= 2 && tls->app_buf[1] == 0) return 0; // close_notify
            return -1;
        }
        if (inner_ctype == 0x17 && pt_len > 0) {
            tls->app_pos = 0;
            tls->app_len = pt_len;
            size_t copy = (len < pt_len) ? len : pt_len;
            memcpy(buf, tls->app_buf, copy);
            tls->app_pos = copy;
            if (tls->app_pos == tls->app_len) {
                tls->app_pos = 0;
                tls->app_len = 0;
            }
            free(rec);
            return (int)copy;
        }
    }
}

void tls_destroy_session(tls_session_t *tls) {
    if (tls) {
        free(tls);
    }
}
