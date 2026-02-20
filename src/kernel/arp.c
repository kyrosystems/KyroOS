#include "arp.h"
#include "heap.h"    // For kmalloc
#include "ip.h"      // For local_ip
#include "kstring.h" // For memcpy, memset
#include "log.h"
#include "net.h" // For getting local MAC

#define ARP_CACHE_SIZE 16
static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static uint8_t arp_cache_next_idx = 0;


// extern uint32_t local_ip; // Use the local_ip from ip.c

void arp_init() {
  memset(arp_cache, 0, sizeof(arp_cache));
  klog(LOG_INFO, "ARP initialized.");
}

void arp_update_cache(uint32_t ip_addr, const uint8_t mac_addr[6]) {
  // Check if already in cache
  for (int i = 0; i < ARP_CACHE_SIZE; i++) {
    if (arp_cache[i].ip_addr == ip_addr) {
      memcpy(arp_cache[i].mac_addr, mac_addr, 6);
      // klog(LOG_INFO, "ARP: Updated cache entry.");
      return;
    }
  }

  // Add new entry
  arp_cache[arp_cache_next_idx].ip_addr = ip_addr;
  memcpy(arp_cache[arp_cache_next_idx].mac_addr, mac_addr, 6);
  arp_cache_next_idx = (arp_cache_next_idx + 1) % ARP_CACHE_SIZE;
  // klog(LOG_INFO, "ARP: Added new cache entry.");
}

uint8_t *arp_lookup_mac(uint32_t ip_addr) {
  for (int i = 0; i < ARP_CACHE_SIZE; i++) {
    if (arp_cache[i].ip_addr == ip_addr) {
      return arp_cache[i].mac_addr;
    }
  }
  return NULL;
}

void arp_send_request(uint32_t target_ip) {
  if (!network_devices) {
    klog(LOG_WARN, "ARP: No network device to send request.");
    return;
  }

  // Allocate packet buffer
  size_t packet_size = sizeof(ethernet_header_t) + sizeof(arp_packet_t);
  uint8_t *packet_buffer = (uint8_t *)kmalloc(packet_size);
  if (!packet_buffer) {
    klog(LOG_ERROR, "ARP: Failed to allocate packet buffer for request.");
    return;
  }
  memset(packet_buffer, 0, packet_size);

  ethernet_header_t *eth_hdr = (ethernet_header_t *)packet_buffer;
  arp_packet_t *arp_pkt =
      (arp_packet_t *)(packet_buffer + sizeof(ethernet_header_t));

  // Ethernet Header
  memset(eth_hdr->dest_mac, 0xFF, 6); // Broadcast
  memcpy(eth_hdr->src_mac, network_devices->mac_addr, 6);
  eth_hdr->ether_type =
      __builtin_bswap16(ARP_ETHER_TYPE); // Host to Network byte order

  // ARP Packet
  arp_pkt->hardware_type = __builtin_bswap16(ARP_HARDWARE_TYPE_ETHERNET);
  arp_pkt->protocol_type = __builtin_bswap16(ARP_PROTOCOL_TYPE_IPV4);
  arp_pkt->hw_addr_len = ARP_HW_ADDR_LEN_ETHERNET;
  arp_pkt->pr_addr_len = ARP_PR_ADDR_LEN_IPV4;
  arp_pkt->opcode = __builtin_bswap16(ARP_OPCODE_REQUEST);
  memcpy(arp_pkt->sender_mac, network_devices->mac_addr, 6);
  arp_pkt->sender_ip =
      __builtin_bswap32(ip_get_local_ip()); // Use IP layer's local_ip
  memset(arp_pkt->target_mac, 0x00, 6);     // Unknown for request
  arp_pkt->target_ip = __builtin_bswap32(target_ip);

  network_devices->send_packet(network_devices, packet_buffer, packet_size);
  // klog(LOG_INFO, "ARP: Sent request for IP: XXX.XXX.XXX.XXX"); // Needs
  // proper IP printing
  kfree(packet_buffer);
}

void arp_handle_packet(const uint8_t *packet, size_t size) {
  if (size < sizeof(arp_packet_t)) {
    klog(LOG_WARN, "ARP: Packet too small.");
    return;
  }

  const arp_packet_t *arp_pkt = (const arp_packet_t *)packet;

  // Check ARP packet validity
  if (__builtin_bswap16(arp_pkt->hardware_type) != ARP_HARDWARE_TYPE_ETHERNET ||
      __builtin_bswap16(arp_pkt->protocol_type) != ARP_PROTOCOL_TYPE_IPV4 ||
      arp_pkt->hw_addr_len != ARP_HW_ADDR_LEN_ETHERNET ||
      arp_pkt->pr_addr_len != ARP_PR_ADDR_LEN_IPV4) {
    klog(LOG_WARN, "ARP: Invalid packet format.");
    return;
  }

  uint32_t sender_ip = __builtin_bswap32(arp_pkt->sender_ip);
  uint32_t target_ip = __builtin_bswap32(arp_pkt->target_ip);
  uint16_t opcode = __builtin_bswap16(arp_pkt->opcode);

  // Update ARP cache with sender's info
  arp_update_cache(sender_ip, arp_pkt->sender_mac);

  if (opcode == ARP_OPCODE_REQUEST) {
    if (target_ip == ip_get_local_ip()) { // Check against IP layer's local_ip
      // klog(LOG_INFO, "ARP: Received request for our IP, sending reply.");

      // Construct and send ARP reply
      if (!network_devices) {
        klog(LOG_WARN, "ARP: No network device to send reply.");
        return;
      }

      size_t reply_size = sizeof(ethernet_header_t) + sizeof(arp_packet_t);
      uint8_t *reply_buffer = (uint8_t *)kmalloc(reply_size);
      if (!reply_buffer) {
        klog(LOG_ERROR, "ARP: Failed to allocate reply buffer.");
        return;
      }
      memset(reply_buffer, 0, reply_size);

      ethernet_header_t *eth_hdr = (ethernet_header_t *)reply_buffer;
      arp_packet_t *arp_reply_pkt =
          (arp_packet_t *)(reply_buffer + sizeof(ethernet_header_t));

      // Ethernet Header
      memcpy(eth_hdr->dest_mac, arp_pkt->sender_mac, 6); // Reply to sender
      memcpy(eth_hdr->src_mac, network_devices->mac_addr, 6);
      eth_hdr->ether_type = __builtin_bswap16(ARP_ETHER_TYPE);

      // ARP Packet (reply)
      arp_reply_pkt->hardware_type =
          __builtin_bswap16(ARP_HARDWARE_TYPE_ETHERNET);
      arp_reply_pkt->protocol_type = __builtin_bswap16(ARP_PROTOCOL_TYPE_IPV4);
      arp_reply_pkt->hw_addr_len = ARP_HW_ADDR_LEN_ETHERNET;
      arp_reply_pkt->pr_addr_len = ARP_PR_ADDR_LEN_IPV4;
      arp_reply_pkt->opcode = __builtin_bswap16(ARP_OPCODE_REPLY);
      memcpy(arp_reply_pkt->sender_mac, network_devices->mac_addr, 6);
      arp_reply_pkt->sender_ip =
          __builtin_bswap32(ip_get_local_ip()); // Use IP layer's local_ip
      memcpy(arp_reply_pkt->target_mac, arp_pkt->sender_mac, 6);
      arp_reply_pkt->target_ip = __builtin_bswap32(sender_ip);

      network_devices->send_packet(network_devices, reply_buffer, reply_size);
      kfree(reply_buffer);
    }
  } else if (opcode == ARP_OPCODE_REPLY) {
    // klog(LOG_INFO, "ARP: Received reply, cache updated.");
  }
}