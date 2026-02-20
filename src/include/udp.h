#ifndef UDP_H
#define UDP_H

#include "ip.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint16_t src_port;
  uint16_t dest_port;
  uint16_t length;
  uint16_t checksum;
} __attribute__((packed)) udp_header_t;

typedef void (*udp_handler_t)(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                              udp_header_t *udp_hdr, const uint8_t *data,
                              size_t len);

void udp_init();
void udp_handle_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                       const uint8_t *packet, size_t size);
void udp_send_packet(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port,
                     const uint8_t *data, size_t len);
void udp_register_handler(uint16_t port, udp_handler_t handler);

#endif
