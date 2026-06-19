#ifndef ARP_H
#define ARP_H

#include <stdint.h>
#include <stddef.h>

#define ARP_ETHER_TYPE              0x0806
#define ARP_HARDWARE_TYPE_ETHERNET  0x0001
#define ARP_PROTOCOL_TYPE_IPV4      0x0800
#define ARP_HW_ADDR_LEN_ETHERNET    0x06
#define ARP_PR_ADDR_LEN_IPV4        0x04

#define ARP_OPCODE_REQUEST  0x0001
#define ARP_OPCODE_REPLY    0x0002

#define ARP_PENDING_QUEUE_SIZE  8
#define ARP_PENDING_MAX_PKT_LEN 2048

typedef struct ethernet_header {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ether_type;
} __attribute__((packed)) ethernet_header_t;

typedef struct arp_packet {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t  hw_addr_len;
    uint8_t  pr_addr_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} __attribute__((packed)) arp_packet_t;

typedef struct arp_cache_entry {
    uint32_t ip_addr;
    uint8_t  mac_addr[6];
} arp_cache_entry_t;

/* Pending packet waiting for ARP resolution */
typedef struct arp_pending_entry {
    uint32_t dest_ip;
    uint8_t  data[ARP_PENDING_MAX_PKT_LEN];
    size_t   len;
    int      used;
} arp_pending_entry_t;

void     arp_init();
void     arp_handle_packet(const uint8_t *packet, size_t size);
void     arp_send_request(uint32_t target_ip);
uint8_t *arp_lookup_mac(uint32_t ip_addr);
void     arp_queue_packet(uint32_t dest_ip, const uint8_t *data, size_t len);
void     arp_flush_pending(uint32_t resolved_ip, const uint8_t *mac);
void     arp_update_cache(uint32_t ip_addr, const uint8_t mac_addr[6]);

#endif // ARP_H
