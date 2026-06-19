#include "arp.h"
#include "heap.h"
#include "ip.h"
#include "kstring.h"
#include "log.h"
#include "net.h"

#define ARP_CACHE_SIZE 16
static arp_cache_entry_t  arp_cache[ARP_CACHE_SIZE];
static uint8_t            arp_cache_next_idx = 0;

static arp_pending_entry_t arp_pending[ARP_PENDING_QUEUE_SIZE];

void arp_init() {
    memset(arp_cache,   0, sizeof(arp_cache));
    memset(arp_pending, 0, sizeof(arp_pending));
    klog(LOG_INFO, "ARP initialized.");
}

void arp_update_cache(uint32_t ip_addr, const uint8_t mac_addr[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].ip_addr == ip_addr) {
            memcpy(arp_cache[i].mac_addr, mac_addr, 6);
            return;
        }
    }
    arp_cache[arp_cache_next_idx].ip_addr = ip_addr;
    memcpy(arp_cache[arp_cache_next_idx].mac_addr, mac_addr, 6);
    arp_cache_next_idx = (arp_cache_next_idx + 1) % ARP_CACHE_SIZE;
}

uint8_t *arp_lookup_mac(uint32_t ip_addr) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].ip_addr == ip_addr)
            return arp_cache[i].mac_addr;
    }
    return NULL;
}

/* Queue a raw Ethernet frame (already built by ip_send_packet) to be sent
   once we get the ARP reply for dest_ip. */
void arp_queue_packet(uint32_t dest_ip, const uint8_t *data, size_t len) {
    if (len > ARP_PENDING_MAX_PKT_LEN) {
        klog(LOG_WARN, "ARP: Pending packet too large, dropped.");
        return;
    }
    for (int i = 0; i < ARP_PENDING_QUEUE_SIZE; i++) {
        if (!arp_pending[i].used) {
            arp_pending[i].dest_ip = dest_ip;
            arp_pending[i].len     = len;
            arp_pending[i].used    = 1;
            memcpy(arp_pending[i].data, data, len);
            klog(LOG_INFO, "ARP: Queued packet for pending resolution.");
            return;
        }
    }
    klog(LOG_WARN, "ARP: Pending queue full, packet dropped.");
}

/* Called when we receive an ARP reply — flush all queued packets for that IP. */
void arp_flush_pending(uint32_t resolved_ip, const uint8_t *mac) {
    for (int i = 0; i < ARP_PENDING_QUEUE_SIZE; i++) {
        if (arp_pending[i].used && arp_pending[i].dest_ip == resolved_ip) {
            /* Patch destination MAC directly into the Ethernet header */
            ethernet_header_t *eth = (ethernet_header_t *)arp_pending[i].data;
            memcpy(eth->dest_mac, mac, 6);
            network_devices->send_packet(network_devices,
                                         arp_pending[i].data,
                                         arp_pending[i].len);
            klog(LOG_INFO, "ARP: Flushed pending packet after resolution.");
            arp_pending[i].used = 0;
        }
    }
}

void arp_send_request(uint32_t target_ip) {
    if (!network_devices) {
        klog(LOG_WARN, "ARP: No network device to send request.");
        return;
    }
    size_t   packet_size  = sizeof(ethernet_header_t) + sizeof(arp_packet_t);
    uint8_t *packet_buffer = (uint8_t *)kmalloc(packet_size);
    if (!packet_buffer) {
        klog(LOG_ERROR, "ARP: Failed to allocate packet buffer.");
        return;
    }
    memset(packet_buffer, 0, packet_size);

    ethernet_header_t *eth_hdr = (ethernet_header_t *)packet_buffer;
    arp_packet_t      *arp_pkt = (arp_packet_t *)(packet_buffer + sizeof(ethernet_header_t));

    memset(eth_hdr->dest_mac, 0xFF, 6);
    memcpy(eth_hdr->src_mac, network_devices->mac_addr, 6);
    eth_hdr->ether_type = __builtin_bswap16(ARP_ETHER_TYPE);

    arp_pkt->hardware_type = __builtin_bswap16(ARP_HARDWARE_TYPE_ETHERNET);
    arp_pkt->protocol_type = __builtin_bswap16(ARP_PROTOCOL_TYPE_IPV4);
    arp_pkt->hw_addr_len   = ARP_HW_ADDR_LEN_ETHERNET;
    arp_pkt->pr_addr_len   = ARP_PR_ADDR_LEN_IPV4;
    arp_pkt->opcode        = __builtin_bswap16(ARP_OPCODE_REQUEST);
    memcpy(arp_pkt->sender_mac, network_devices->mac_addr, 6);
    arp_pkt->sender_ip = __builtin_bswap32(ip_get_local_ip());
    memset(arp_pkt->target_mac, 0x00, 6);
    arp_pkt->target_ip = __builtin_bswap32(target_ip);

    network_devices->send_packet(network_devices, packet_buffer, packet_size);
    kfree(packet_buffer);
}

void arp_handle_packet(const uint8_t *packet, size_t size) {
    if (size < sizeof(arp_packet_t)) {
        klog(LOG_WARN, "ARP: Packet too small.");
        return;
    }
    const arp_packet_t *arp_pkt = (const arp_packet_t *)packet;

    if (__builtin_bswap16(arp_pkt->hardware_type) != ARP_HARDWARE_TYPE_ETHERNET ||
        __builtin_bswap16(arp_pkt->protocol_type) != ARP_PROTOCOL_TYPE_IPV4    ||
        arp_pkt->hw_addr_len != ARP_HW_ADDR_LEN_ETHERNET                       ||
        arp_pkt->pr_addr_len != ARP_PR_ADDR_LEN_IPV4) {
        klog(LOG_WARN, "ARP: Invalid packet format.");
        return;
    }

    uint32_t sender_ip = __builtin_bswap32(arp_pkt->sender_ip);
    uint32_t target_ip = __builtin_bswap32(arp_pkt->target_ip);
    uint16_t opcode    = __builtin_bswap16(arp_pkt->opcode);

    arp_update_cache(sender_ip, arp_pkt->sender_mac);

    if (opcode == ARP_OPCODE_REQUEST) {
        if (target_ip != ip_get_local_ip()) return;
        if (!network_devices) return;

        size_t   reply_size   = sizeof(ethernet_header_t) + sizeof(arp_packet_t);
        uint8_t *reply_buffer = (uint8_t *)kmalloc(reply_size);
        if (!reply_buffer) return;
        memset(reply_buffer, 0, reply_size);

        ethernet_header_t *eth_hdr       = (ethernet_header_t *)reply_buffer;
        arp_packet_t      *arp_reply_pkt = (arp_packet_t *)(reply_buffer + sizeof(ethernet_header_t));

        memcpy(eth_hdr->dest_mac, arp_pkt->sender_mac, 6);
        memcpy(eth_hdr->src_mac,  network_devices->mac_addr, 6);
        eth_hdr->ether_type = __builtin_bswap16(ARP_ETHER_TYPE);

        arp_reply_pkt->hardware_type = __builtin_bswap16(ARP_HARDWARE_TYPE_ETHERNET);
        arp_reply_pkt->protocol_type = __builtin_bswap16(ARP_PROTOCOL_TYPE_IPV4);
        arp_reply_pkt->hw_addr_len   = ARP_HW_ADDR_LEN_ETHERNET;
        arp_reply_pkt->pr_addr_len   = ARP_PR_ADDR_LEN_IPV4;
        arp_reply_pkt->opcode        = __builtin_bswap16(ARP_OPCODE_REPLY);
        memcpy(arp_reply_pkt->sender_mac, network_devices->mac_addr, 6);
        arp_reply_pkt->sender_ip = __builtin_bswap32(ip_get_local_ip());
        memcpy(arp_reply_pkt->target_mac, arp_pkt->sender_mac, 6);
        arp_reply_pkt->target_ip = __builtin_bswap32(sender_ip);

        network_devices->send_packet(network_devices, reply_buffer, reply_size);
        kfree(reply_buffer);

    } else if (opcode == ARP_OPCODE_REPLY) {
        /* Flush any packets that were waiting for this resolution */
        arp_flush_pending(sender_ip, arp_pkt->sender_mac);
        klog(LOG_INFO, "ARP: Reply received, cache updated and pending flushed.");
    }
}
