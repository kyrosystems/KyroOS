# TLS/HTTPS Support in KyroOS

KyroOS includes native client-side **TLS 1.3** and **HTTPS** support via `userspace/lib/libtls` and userspace networking tools (`curl`, `wget`).

## Overview

The freestanding TLS 1.3 implementation provides:
- **TLS Protocol**: TLS 1.3 (RFC 8446) client handshake.
- **Key Exchange**: Ephemeral Diffie-Hellman over Curve25519 (X25519).
- **Key Derivation**: HKDF-Extract and HKDF-Expand with SHA-256 (RFC 5869).
- **Authenticated Encryption with Associated Data (AEAD)**:
  - ChaCha20-Poly1305 (RFC 8439)
  - AES-128-GCM and AES-256-GCM
- **Record Layer**:
  - Handshake and Application Data records (content type 0x17 with inner type encoding).
  - Explicit sequence number tracking and per-record nonce construction (`nonce ^ seq_num`).
- **Client Tools**:
  - `curl`: Supports both `http://` and `https://` URLs with verbose output (`-v`).
  - `wget`: Supports file downloads over `https://` and `http://`.

## Handshake Flow

1. **ClientHello**:
   - Sent by client advertising TLS 1.3 cipher suites (`TLS_AES_128_GCM_SHA256`, `TLS_CHACHA20_POLY1305_SHA256`).
   - Extensions: `supported_versions` (0x0304), `server_name` (SNI), `supported_groups` (X25519), `key_share` with ephemeral X25519 public key.
2. **ServerHello**:
   - Server returns matching X25519 key share and chosen cipher suite.
3. **Secret Derivation**:
   - Shared secret computed via X25519 scalar multiplication.
   - Handshake secret derived via HKDF-Extract.
   - `c_hs_traffic` (client handshake traffic secret) and `s_hs_traffic` (server handshake traffic secret) derived via HKDF-Expand-Label.
4. **Encrypted Handshake & Application Traffic**:
   - EncryptedExtensions, Certificate, CertificateVerify, Finished processed.
   - Application traffic secrets derived for full bidirectional secure communication.

## Integration

Libraries link against `userspace/lib/libtls` (`tls.h`). Sessions are created using:
```c
tls_session_t *tls = tls_create_session(sockfd, hostname);
if (tls_handshake(tls) == 0) {
    tls_write(tls, data, len);
    int n = tls_read(tls, buf, sizeof(buf));
}
tls_destroy_session(tls);
```
