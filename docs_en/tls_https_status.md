# TLS/HTTPS status in KyroOS (honest)

**Not implemented.** The kernel crypto module provides SHA-256 only
(`SYS_SHA256`). TLS 1.2/1.3 requires X.509 parsing, ECDHE/RSA key exchange,
AES-GCM/ChaCha20-Poly1305, a record layer and a handshake state machine —
none of which exist.

The planned path is porting a vetted library (mbedTLS/wolfSSL), not a
home-grown stack. Until then `wget`/`curl` work over plain HTTP only and
MUST NOT present HTTPS as functional. The system trust store location
(`/etc/ssl/certs`) is reserved for the future stack.

See also: `abi/posix_matrix.md`.
