#ifndef _LIBTLS_H
#define _LIBTLS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TLS_CIPHER_AES_128_GCM_SHA256        0x1301
#define TLS_CIPHER_CHACHA20_POLY1305_SHA256  0x1303

typedef struct tls_session tls_session_t;

struct tls_session {
    int sockfd;
    char hostname[256];
    uint16_t cipher_suite;
    uint8_t client_random[32];
    uint8_t server_random[32];
    uint8_t client_private_key[32];
    uint8_t client_public_key[32];
    uint8_t server_public_key[32];
    uint8_t shared_secret[32];
    uint8_t client_write_key[32];
    uint8_t server_write_key[32];
    uint8_t client_write_iv[12];
    uint8_t server_write_iv[12];
    uint64_t client_seq;
    uint64_t server_seq;
    bool is_established;
    bool eof;
    uint8_t app_buf[18432];
    size_t app_pos;
    size_t app_len;
};

tls_session_t *tls_create_session(int sockfd, const char *hostname);
int tls_handshake(tls_session_t *tls);
int tls_send(tls_session_t *tls, const void *buf, size_t len);
int tls_recv(tls_session_t *tls, void *buf, size_t len);
void tls_destroy_session(tls_session_t *tls);

#endif
