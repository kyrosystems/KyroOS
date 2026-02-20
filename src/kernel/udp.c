#include "udp.h"
#include "heap.h"
#include "ip.h"
#include "kstring.h"
#include "log.h"

#define UDP_MAX_HANDLERS 64
static struct {
  uint16_t port;
  udp_handler_t handler;
} udp_handlers[UDP_MAX_HANDLERS];

static int udp_handler_count = 0;

void udp_init() {
  memset(udp_handlers, 0, sizeof(udp_handlers));
  udp_handler_count = 0;
  ip_register_protocol_handler(IP_PROTOCOL_UDP, udp_handle_packet);
  klog(LOG_INFO, "UDP layer initialized.");
}

void udp_register_handler(uint16_t port, udp_handler_t handler) {
  if (udp_handler_count < UDP_MAX_HANDLERS) {
    udp_handlers[udp_handler_count].port = port;
    udp_handlers[udp_handler_count].handler = handler;
    udp_handler_count++;
  }
}

void udp_handle_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                       const uint8_t *packet, size_t size) {
  if (size < sizeof(udp_header_t)) {
    klog(LOG_WARN, "UDP: Packet too small.");
    return;
  }

  udp_header_t *udp_hdr = (udp_header_t *)packet;
  uint16_t dest_port = __builtin_bswap16(udp_hdr->dest_port);
  uint16_t length = __builtin_bswap16(udp_hdr->length);

  if (length > size) {
    klog(LOG_WARN, "UDP: Invalid length field.");
    return;
  }

  const uint8_t *data = packet + sizeof(udp_header_t);
  size_t data_len = length - sizeof(udp_header_t);

  for (int i = 0; i < udp_handler_count; i++) {
    if (udp_handlers[i].port == dest_port) {
      udp_handlers[i].handler(net_dev, (ipv4_header_t *)ip_hdr, udp_hdr, data,
                              data_len);
      return;
    }
  }

  char buf[64];
  ksprintf(buf, "UDP: No handler for port %d", (int)dest_port);
  klog(LOG_INFO, "%s", buf);
}

void udp_send_packet(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port,
                     const uint8_t *data, size_t len) {
  size_t udp_size = sizeof(udp_header_t) + len;
  uint8_t *buffer = (uint8_t *)kmalloc(udp_size);
  if (!buffer)
    return;

  udp_header_t *udp_hdr = (udp_header_t *)buffer;
  udp_hdr->src_port = __builtin_bswap16(src_port);
  udp_hdr->dest_port = __builtin_bswap16(dest_port);
  udp_hdr->length = __builtin_bswap16(udp_size);
  udp_hdr->checksum = 0; // Optional for IPv4

  memcpy(buffer + sizeof(udp_header_t), data, len);

  ip_send_packet(network_devices, dest_ip, IP_PROTOCOL_UDP, buffer, udp_size);
  kfree(buffer);
}
