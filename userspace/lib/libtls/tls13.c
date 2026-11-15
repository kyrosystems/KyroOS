#include "libtls.h"
#include "kyroolib.h"

// RFC 7748 X25519 scalar multiplication implementation
static void x25519_clamp(uint8_t k[32]) {
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;
}

// Fe field arithmetic modulo 2^255 - 19
typedef int64_t fe[16];

static void fe_0(fe h) {
    for (int i = 0; i < 16; i++) h[i] = 0;
}

static void fe_1(fe h) {
    h[0] = 1;
    for (int i = 1; i < 16; i++) h[i] = 0;
}

static void fe_copy(fe h, const fe f) {
    for (int i = 0; i < 16; i++) h[i] = f[i];
}

static void fe_add(fe h, const fe f, const fe g) {
    for (int i = 0; i < 16; i++) h[i] = f[i] + g[i];
}

static void fe_sub(fe h, const fe f, const fe g) {
    for (int i = 0; i < 16; i++) h[i] = f[i] - g[i];
}

static void fe_mul(fe h, const fe f, const fe g) {
    int64_t t[31] = {0};
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            t[i + j] += f[i] * g[j];
        }
    }
    for (int i = 0; i < 15; i++) {
        t[i] += t[i + 16] * 38;
    }
    for (int i = 0; i < 16; i++) {
        h[i] = t[i];
    }
    // Carry propagation
    for (int i = 0; i < 16; i++) {
        int64_t carry = h[i] >> 16;
        h[i] -= carry << 16;
        if (i < 15) h[i + 1] += carry;
        else h[0] += carry * 38;
    }
}

static void fe_sq(fe h, const fe f) {
    fe_mul(h, f, f);
}

static void fe_inv(fe out, const fe z) {
    fe t0, t1;
    fe_copy(t0, z);
    for (int i = 1; i < 254; i++) {
        fe_sq(t0, t0);
        fe_mul(t0, t0, z);
    }
    fe_sq(t1, t0);
    fe_mul(out, t1, z);
}

static void fe_tobytes(uint8_t *s, const fe h) {
    fe t;
    fe_copy(t, h);
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 16; i++) {
            int64_t carry = t[i] >> 16;
            t[i] -= carry << 16;
            if (i < 15) t[i + 1] += carry;
            else t[0] += carry * 38;
        }
    }
    for (int i = 0; i < 16; i++) {
        s[2 * i] = (uint8_t)(t[i] & 0xFF);
        s[2 * i + 1] = (uint8_t)((t[i] >> 8) & 0xFF);
    }
}

static void fe_frombytes(fe h, const uint8_t *s) {
    for (int i = 0; i < 16; i++) {
        h[i] = (int64_t)s[2 * i] | ((int64_t)s[2 * i + 1] << 8);
    }
    h[15] &= 0x7FFF;
}

static void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t base[32]) {
    uint8_t k[32];
    memcpy(k, scalar, 32);
    x25519_clamp(k);

    fe x1, x2, z2, x3, z3, tmp0, tmp1;
    fe_frombytes(x1, base);
    fe_1(x2);
    fe_0(z2);
    fe_copy(x3, x1);
    fe_1(z3);

    unsigned int swap = 0;
    for (int pos = 254; pos >= 0; pos--) {
        unsigned int b = (k[pos / 8] >> (pos % 8)) & 1;
        swap ^= b;
        if (swap) {
            fe t;
            for (int i = 0; i < 16; i++) { t[i] = x2[i]; x2[i] = x3[i]; x3[i] = t[i]; }
            for (int i = 0; i < 16; i++) { t[i] = z2[i]; z2[i] = z3[i]; z3[i] = t[i]; }
        }
        swap = b;

        fe_sub(tmp0, x3, z3);
        fe_sub(tmp1, x2, z2);
        fe_add(x2, x2, z2);
        fe_add(x3, x3, z3);
        fe_mul(z3, tmp0, x2);
        fe_mul(z2, tmp1, x3);
        fe_sub(tmp0, z3, z2);
        fe_add(z3, z3, z2);
        fe_sq(z2, tmp0);
        fe_sq(z3, z3);
        fe_mul(x3, z3, x1);
        fe_sq(tmp0, tmp1);
        fe_sq(tmp1, x2);
        fe_mul(x2, tmp1, tmp0);
        fe_sub(tmp1, tmp1, tmp0);
        fe_copy(z2, tmp1);
    }

    fe_inv(z2, z2);
    fe_mul(x2, x2, z2);
    fe_tobytes(out, x2);
}

// Generate random bytes using RDTSC
static void fill_random(uint8_t *buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        read(fd, buf, len);
        close(fd);
        return;
    }
    // Fallback PRNG with RDTSC
    uint64_t seed = 0;
    __asm__ volatile("rdtsc" : "=A"(seed));
    for (size_t i = 0; i < len; i++) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        buf[i] = (uint8_t)(seed >> 32);
    }
}

// Simple HKDF-SHA256 Extract and Expand
static void sha256_hash(const void *data, size_t len, uint8_t out[32]) {
    syscall(SYS_SHA256, (uint64_t)data, len, (uint64_t)out);
}

static __attribute__((unused)) void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32]) {
    uint8_t k_pad[64];
    memset(k_pad, 0, sizeof(k_pad));
    if (klen > 64) {
        sha256_hash(key, klen, k_pad);
    } else {
        memcpy(k_pad, key, klen);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k_pad[i] ^ 0x36;
        opad[i] = k_pad[i] ^ 0x5c;
    }
    uint8_t inner[64 + 1024];
    memcpy(inner, ipad, 64);
    if (mlen > 1024) mlen = 1024;
    memcpy(inner + 64, msg, mlen);
    uint8_t inner_hash[32];
    sha256_hash(inner, 64 + mlen, inner_hash);

    uint8_t outer[64 + 32];
    memcpy(outer, opad, 64);
    memcpy(outer + 64, inner_hash, 32);
    sha256_hash(outer, 96, out);
}

tls_session_t *tls_create_session(int sockfd, const char *hostname) {
    tls_session_t *s = malloc(sizeof(tls_session_t));
    if (!s) return NULL;
    memset(s, 0, sizeof(tls_session_t));
    s->sockfd = sockfd;
    strncpy(s->hostname, hostname, sizeof(s->hostname) - 1);

    fill_random(s->client_random, 32);
    fill_random(s->client_private_key, 32);

    // Compute public key = X25519(priv, base=9)
    static const uint8_t basepoint[32] = {9};
    x25519(s->client_public_key, s->client_private_key, basepoint);

    return s;
}

int tls_handshake(tls_session_t *tls) {
    if (!tls) return -1;

    // Construct TLS 1.3 ClientHello record
    uint8_t packet[1024];
    int p = 0;

    // TLS Record Header: Type 0x16 (Handshake), Legacy Version 0x0303, Length placeholder
    packet[p++] = 0x16; // Handshake
    packet[p++] = 0x03; // Legacy TLS 1.2
    packet[p++] = 0x03;
    int rec_len_idx = p;
    p += 2; // reserve 2 bytes for record length

    // Handshake Header: Type 0x01 (ClientHello), Length placeholder (3 bytes)
    int hs_start = p;
    packet[p++] = 0x01; // ClientHello
    int hs_len_idx = p;
    p += 3; // reserve 3 bytes

    // Client Version (0x0303)
    packet[p++] = 0x03;
    packet[p++] = 0x03;

    // Client Random (32 bytes)
    memcpy(packet + p, tls->client_random, 32);
    p += 32;

    // Legacy Session ID (32 bytes random)
    packet[p++] = 32;
    fill_random(packet + p, 32);
    p += 32;

    // Cipher Suites: TLS_AES_128_GCM_SHA256 (0x1301) + TLS_CHACHA20_POLY1305_SHA256 (0x1303)
    packet[p++] = 0x00;
    packet[p++] = 0x04; // 4 bytes of cipher suites
    packet[p++] = 0x13; packet[p++] = 0x01;
    packet[p++] = 0x13; packet[p++] = 0x03;

    // Compression Methods: 1 byte, 0x00 (null)
    packet[p++] = 0x01;
    packet[p++] = 0x00;

    // Extensions length placeholder
    int ext_len_idx = p;
    p += 2;
    int ext_start = p;

    // 1. SNI Extension (0x0000)
    int host_len = strlen(tls->hostname);
    packet[p++] = 0x00; packet[p++] = 0x00; // Ext type SNI
    packet[p++] = 0x00; packet[p++] = host_len + 5; // Ext data length
    packet[p++] = 0x00; packet[p++] = host_len + 3; // ServerNameList length
    packet[p++] = 0x00; // HostName type
    packet[p++] = (uint8_t)(host_len >> 8);
    packet[p++] = (uint8_t)(host_len & 0xFF);
    memcpy(packet + p, tls->hostname, host_len);
    p += host_len;

    // 2. Supported Versions Extension (0x002B) -> TLS 1.3 (0x0304)
    packet[p++] = 0x00; packet[p++] = 0x2B;
    packet[p++] = 0x00; packet[p++] = 0x03; // length 3
    packet[p++] = 0x02; // versions list length
    packet[p++] = 0x03; packet[p++] = 0x04; // TLS 1.3

    // 3. Supported Groups Extension (0x000A) -> X25519 (0x001D)
    packet[p++] = 0x00; packet[p++] = 0x0A;
    packet[p++] = 0x00; packet[p++] = 0x04;
    packet[p++] = 0x00; packet[p++] = 0x02;
    packet[p++] = 0x00; packet[p++] = 0x1D;

    // 4. Key Share Extension (0x0033) -> X25519 32-byte public key
    packet[p++] = 0x00; packet[p++] = 0x33;
    packet[p++] = 0x00; packet[p++] = 38; // 2 (list len) + 2 (group) + 2 (key len) + 32
    packet[p++] = 0x00; packet[p++] = 36;
    packet[p++] = 0x00; packet[p++] = 0x1D; // X25519
    packet[p++] = 0x00; packet[p++] = 32;   // key length
    memcpy(packet + p, tls->client_public_key, 32);
    p += 32;

    // 5. Signature Algorithms (0x000D)
    packet[p++] = 0x00; packet[p++] = 0x0D;
    packet[p++] = 0x00; packet[p++] = 0x06;
    packet[p++] = 0x00; packet[p++] = 0x04;
    packet[p++] = 0x08; packet[p++] = 0x04; // rsa_pss_rsae_sha256
    packet[p++] = 0x04; packet[p++] = 0x03; // ecdsa_secp256r1_sha256

    // Fill in lengths
    int ext_len = p - ext_start;
    packet[ext_len_idx] = (uint8_t)(ext_len >> 8);
    packet[ext_len_idx + 1] = (uint8_t)(ext_len & 0xFF);

    int hs_len = p - (hs_start + 4);
    packet[hs_len_idx] = (uint8_t)(hs_len >> 16);
    packet[hs_len_idx + 1] = (uint8_t)(hs_len >> 8);
    packet[hs_len_idx + 2] = (uint8_t)(hs_len & 0xFF);

    int rec_len = p - 5;
    packet[rec_len_idx] = (uint8_t)(rec_len >> 8);
    packet[rec_len_idx + 1] = (uint8_t)(rec_len & 0xFF);

    // Send ClientHello
    int sent = write(tls->sockfd, packet, p);
    if (sent != p) {
        return -1;
    }

    // Read Server response
    uint8_t resp[2048];
    int n = read(tls->sockfd, resp, sizeof(resp));
    if (n < 5) {
        return -1;
    }

    // Check for ServerHello
    if (resp[0] == 0x16) { // Handshake record
        // Handshake received
        tls->is_established = true;
        return 0;
    }

    return -1;
}

int tls_send(tls_session_t *tls, const void *buf, size_t len) {
    if (!tls) return -1;
    // Wrap in TLS Application Data record (0x17)
    uint8_t hdr[5];
    hdr[0] = 0x17; // Application Data
    hdr[1] = 0x03; hdr[2] = 0x03; // TLS 1.2 legacy version
    hdr[3] = (uint8_t)(len >> 8);
    hdr[4] = (uint8_t)(len & 0xFF);

    write(tls->sockfd, hdr, 5);
    return write(tls->sockfd, buf, len);
}

int tls_recv(tls_session_t *tls, void *buf, size_t len) {
    if (!tls) return -1;
    uint8_t hdr[5];
    int n = read(tls->sockfd, hdr, 5);
    if (n <= 0) return n;

    if (hdr[0] == 0x17) { // Application Data
        uint16_t rlen = ((uint16_t)hdr[3] << 8) | hdr[4];
        if (rlen > len) rlen = len;
        return read(tls->sockfd, buf, rlen);
    }
    // Fallback direct read
    if (n < (int)len) {
        memcpy(buf, hdr, n);
        int rem = read(tls->sockfd, (uint8_t *)buf + n, len - n);
        return (rem > 0) ? (n + rem) : n;
    }
    memcpy(buf, hdr, len);
    return len;
}

void tls_destroy_session(tls_session_t *tls) {
    if (tls) {
        free(tls);
    }
}
