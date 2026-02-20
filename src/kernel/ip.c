#include "ip.h"
#include "arp.h"
#include "heap.h"
#include "icmp.h"    // Include ICMP header
#include "kstring.h" // For memcpy, memset
#include "log.h"
#include "net.h"
#include "socket.h" // For sock_handle_incoming_packet
void sock_handle_incoming_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr, const uint8_t *payload, size_t payload_size, uint8_t protocol); // Temporary explicit prototype

// Our local IP address
static uint32_t local_ip;
static uint32_t subnet_mask;
static uint32_t default_gateway;

void ip_set_local_ip(uint32_t ip) {
    local_ip = ip;
}

void ip_set_subnet_mask(uint32_t mask) {
    subnet_mask = mask;
}

void ip_set_default_gateway(uint32_t gateway) {
    default_gateway = gateway;
}

// Protocol handlers (indexed by IP protocol number)
static ip_protocol_handler_t ip_protocol_handlers[256] = {0};

void ip_init() {
  memset(ip_protocol_handlers, 0, sizeof(ip_protocol_handlers));
  // Only register ICMP here, UDP and TCP will be handled by the socket layer
  ip_register_protocol_handler(IP_PROTOCOL_ICMP, icmp_handle_packet); 
  klog(LOG_INFO, "IP layer initialized.");
}

uint32_t ip_get_local_ip() { return local_ip; }

void ip_register_protocol_handler(uint8_t protocol,
                                  ip_protocol_handler_t handler) {
  ip_protocol_handlers[protocol] = handler;
}

// Simple IP checksum calculation (RFC 1071)
static uint16_t ip_checksum(const void *data, size_t len) {
  const uint16_t *u16_buf = (const uint16_t *)data;
  uint32_t sum = 0;
  while (len > 1) {
    sum += *u16_buf++;
    len -= 2;
  }
  if (len == 1) {
    sum += *(const uint8_t *)u16_buf;
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return (uint16_t)~sum;
}

void ip_send_packet(net_dev_t *net_dev, uint32_t dest_ip, uint8_t protocol,
                    const uint8_t *payload, size_t payload_size) {
  uint8_t *dest_mac;
  uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  if (dest_ip == 0xFFFFFFFF) {
    dest_mac = broadcast_mac;
  } else {
    dest_mac = arp_lookup_mac(dest_ip);
    if (!dest_mac) {
      klog(LOG_WARN, "IP: MAC address not found for destination IP, sending ARP request.");
      arp_send_request(dest_ip);
      return; // Cannot send packet without MAC
    }
  }

  size_t ip_packet_size = sizeof(ipv4_header_t) + payload_size;
  size_t eth_packet_size = sizeof(ethernet_header_t) + ip_packet_size;

  uint8_t *packet_buffer = (uint8_t *)kmalloc(eth_packet_size);
  if (!packet_buffer) {
    klog(LOG_ERROR, "IP: Failed to allocate packet buffer.");
    return;
  }
  memset(packet_buffer, 0, eth_packet_size);

  ethernet_header_t *eth_hdr = (ethernet_header_t *)packet_buffer;
  ipv4_header_t *ip_hdr =
      (ipv4_header_t *)(packet_buffer + sizeof(ethernet_header_t));
  uint8_t *ip_payload =
      packet_buffer + sizeof(ethernet_header_t) + sizeof(ipv4_header_t);

  // Ethernet Header
  memcpy(eth_hdr->dest_mac, dest_mac, 6);
  memcpy(eth_hdr->src_mac, net_dev->mac_addr, 6);
  eth_hdr->ether_type = __builtin_bswap16(0x0800); // IPv4 EtherType

  // IPv4 Header
  ip_hdr->version = 4;
  ip_hdr->ihl = sizeof(ipv4_header_t) / 4;
  ip_hdr->dscp = 0;
  ip_hdr->ecn = 0;
  ip_hdr->total_length = __builtin_bswap16(ip_packet_size);
  ip_hdr->identification = __builtin_bswap16(0x0001); // Simple ID
  ip_hdr->flags = 0x0;
  ip_hdr->fragment_offset = 0;
  ip_hdr->time_to_live = 64;
  ip_hdr->protocol = protocol;
  ip_hdr->src_ip = __builtin_bswap32(local_ip);
  ip_hdr->dest_ip = __builtin_bswap32(dest_ip);
  ip_hdr->header_checksum = 0; // Calculate after filling everything
  ip_hdr->header_checksum = ip_checksum(ip_hdr, sizeof(ipv4_header_t));

  // Payload
  memcpy(ip_payload, payload, payload_size);

  net_dev->send_packet(net_dev, packet_buffer, eth_packet_size);
  // klog(LOG_INFO, "IP: Sent packet.");
  kfree(packet_buffer);
}

void ip_handle_packet(net_dev_t *net_dev, const uint8_t *packet, size_t size) {
  if (size < sizeof(ipv4_header_t)) {
    klog(LOG_WARN, "IP: Packet too small.");
    return;
  }

  const ipv4_header_t *ip_hdr = (const ipv4_header_t *)packet;
  size_t ip_hdr_len = ip_hdr->ihl * 4; // Declare once at the beginning

  // Check IP version and IHL
  if (ip_hdr->version != 4) {
    klog(LOG_WARN, "IP: Not IPv4 packet.");
    return;
  }
  if (ip_hdr->ihl < 5) { // Minimum header length is 5 DWORDS = 20 bytes
    klog(LOG_WARN, "IP: IP header too short.");
    return;
  }

  // Check checksum
  if (ip_checksum(ip_hdr, ip_hdr_len) != 0) {
    klog(LOG_WARN, "IP: Invalid header checksum.");
    return;
  }

  uint32_t dest_ip = __builtin_bswap32(ip_hdr->dest_ip);
  if (dest_ip != local_ip &&
      dest_ip != 0xFFFFFFFF) { // Not for us and not broadcast
    return;                    // Silently drop packets not for us
  }

  uint8_t protocol = ip_hdr->protocol;
  const uint8_t *payload = packet + ip_hdr_len;
  size_t payload_size = __builtin_bswap16(ip_hdr->total_length) - ip_hdr_len;

  // Forward to socket layer for processing (UDP/TCP) or specialized handler (ICMP)
  if (protocol == IP_PROTOCOL_ICMP) {
      ip_protocol_handlers[protocol](net_dev, ip_hdr, payload, payload_size);
  } else if (protocol == IPPROTO_UDP || protocol == IPPROTO_TCP) {
      sock_handle_incoming_packet(net_dev, ip_hdr, payload, payload_size, protocol);
  } else {
      klog(LOG_WARN, "IP: No handler for protocol %d.", protocol);
  }
}
