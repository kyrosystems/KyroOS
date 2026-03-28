#ifndef IP_H
#define IP_H

#include <stdint.h>
#include <stddef.h>
#include "net.h" // For net_dev_t

// IPv4 Header Structure
typedef struct ipv4_header {
    uint8_t  ihl:4; // Internet Header Length
    uint8_t  version:4;
    uint8_t  dscp:6; // Differentiated Services Code Point
    uint8_t  ecn:2;  // Explicit Congestion Notification
    uint16_t total_length;
    uint16_t identification;
    uint8_t  flags:3;
    uint16_t fragment_offset:13;
    uint8_t  time_to_live;
    uint8_t  protocol;
    uint16_t header_checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed)) ipv4_header_t;

#define IP_PROTOCOL_ICMP 0x01
#define IP_PROTOCOL_TCP  0x06
#define IP_PROTOCOL_UDP  0x11

void ip_init();
uint32_t ip_get_local_ip();
uint32_t ip_get_subnet_mask();
uint32_t ip_get_default_gateway();
void ip_set_local_ip(uint32_t ip);
void ip_set_subnet_mask(uint32_t mask);
void ip_set_default_gateway(uint32_t gateway);
void ip_handle_packet(net_dev_t* net_dev, const uint8_t* packet, size_t size);
void ip_send_packet(net_dev_t* net_dev, uint32_t dest_ip, uint8_t protocol, const uint8_t* payload, size_t payload_size);

// Function to register a protocol handler (e.g., ICMP, TCP, UDP)
typedef void (*ip_protocol_handler_t)(net_dev_t* net_dev, const ipv4_header_t* ip_hdr, const uint8_t* payload, size_t payload_size);
void ip_register_protocol_handler(uint8_t protocol, ip_protocol_handler_t handler);

#endif // IP_H
