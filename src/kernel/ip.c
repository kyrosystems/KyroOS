#include "ip.h"
#include "arp.h"
#include "heap.h"
#include "icmp.h"    
#include "kstring.h" 
#include "log.h"
#include "net.h"
#include "socket.h" 

void sock_handle_incoming_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr, const uint8_t *payload, size_t payload_size, uint8_t protocol); 

static uint32_t local_ip = 0;
static uint32_t subnet_mask = 0;
static uint32_t default_gateway = 0;

void ip_set_local_ip(uint32_t ip) { local_ip = ip; }
void ip_set_subnet_mask(uint32_t mask) { subnet_mask = mask; }
void ip_set_default_gateway(uint32_t gateway) { default_gateway = gateway; }

uint32_t ip_get_local_ip() { return local_ip; }
uint32_t ip_get_subnet_mask() { return subnet_mask; }
uint32_t ip_get_default_gateway() { return default_gateway; }

static ip_protocol_handler_t ip_protocol_handlers[256] = {0};

void ip_init() {
  memset(ip_protocol_handlers, 0, sizeof(ip_protocol_handlers));
  ip_register_protocol_handler(IP_PROTOCOL_ICMP, icmp_handle_packet); 
  klog(LOG_INFO, "IP layer initialized.");
}

void ip_register_protocol_handler(uint8_t protocol, ip_protocol_handler_t handler) {
  ip_protocol_handlers[protocol] = handler;
}

static uint16_t ip_checksum(const void *data, size_t len) {
  const uint16_t *u16_buf = (const uint16_t *)data;
  uint32_t sum = 0;
  while (len > 1) { sum += *u16_buf++; len -= 2; }
  if (len == 1) sum += *(const uint8_t *)u16_buf;
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
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
      arp_send_request(dest_ip);
      return; 
    }
  }

  size_t ip_size = sizeof(ipv4_header_t) + payload_size;
  size_t eth_size = sizeof(ethernet_header_t) + ip_size;

  uint8_t *buf = (uint8_t *)kmalloc(eth_size);
  if (!buf) return;
  memset(buf, 0, eth_size);

  ethernet_header_t *eth = (ethernet_header_t *)buf;
  ipv4_header_t *ip = (ipv4_header_t *)(buf + sizeof(ethernet_header_t));
  uint8_t *pld = buf + sizeof(ethernet_header_t) + sizeof(ipv4_header_t);

  memcpy(eth->dest_mac, dest_mac, 6);
  memcpy(eth->src_mac, net_dev->mac_addr, 6);
  eth->ether_type = __builtin_bswap16(0x0800);

  ip->version = 4;
  ip->ihl = 5;
  ip->total_length = __builtin_bswap16(ip_size);
  ip->time_to_live = 64;
  ip->protocol = protocol;
  ip->src_ip = __builtin_bswap32(local_ip);
  ip->dest_ip = __builtin_bswap32(dest_ip);
  ip->header_checksum = 0;
  ip->header_checksum = ip_checksum(ip, sizeof(ipv4_header_t));

  memcpy(pld, payload, payload_size);
  net_dev->send_packet(net_dev, buf, eth_size);
  kfree(buf);
}

void ip_handle_packet(net_dev_t *net_dev, const uint8_t *packet, size_t size) {
  if (size < sizeof(ipv4_header_t)) return;
  const ipv4_header_t *ip = (const ipv4_header_t *)packet;
  if (ip->version != 4) return;
  
  uint32_t dip = __builtin_bswap32(ip->dest_ip);
  if (dip != local_ip && dip != 0xFFFFFFFF) return;

  size_t hlen = ip->ihl * 4;
  const uint8_t *payload = packet + hlen;
  size_t psize = __builtin_bswap16(ip->total_length) - hlen;

  if (ip->protocol == IP_PROTOCOL_ICMP) {
      if (ip_protocol_handlers[ip->protocol])
          ip_protocol_handlers[ip->protocol](net_dev, ip, payload, psize);
  } else if (ip->protocol == IP_PROTOCOL_UDP || ip->protocol == IP_PROTOCOL_TCP) {
      sock_handle_incoming_packet(net_dev, ip, payload, psize, ip->protocol);
  }
}
