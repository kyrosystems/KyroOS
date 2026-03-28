#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "net.h" // For net_dev_t
#include "ip.h"  // For ipv4_header_t

// ICMP Types
#define ICMP_ECHO_REPLY   0
#define ICMP_DEST_UNREACH 3
#define ICMP_ECHO_REQUEST 8
#define ICMP_TIME_EXCEEDED 11

// ICMP Header Structure
typedef struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
    // Data follows
} __attribute__((packed)) icmp_header_t;

void icmp_init();
void icmp_handle_packet(net_dev_t* net_dev, const ipv4_header_t* ip_hdr, const uint8_t* payload, size_t payload_size);
void icmp_send_echo_request(net_dev_t* net_dev, uint32_t dest_ip, uint16_t id, uint16_t sequence, const uint8_t* data, size_t data_size);
bool icmp_ping(uint32_t dest_ip, uint32_t timeout_ms);

#endif // ICMP_H
