#include "dhcp.h"
#include "udp.h"
#include "net.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "ip.h"

typedef enum {
    DHCP_STATE_INIT,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND
} dhcp_state_t;

static dhcp_state_t dhcp_state = DHCP_STATE_INIT;
static uint32_t offered_ip = 0;
static uint32_t server_ip = 0;
static uint32_t dhcp_xid = 0x55AA55AA;

static void dhcp_send_packet(uint8_t type, uint32_t requested_ip) {
    if (!network_devices) {
        klog(LOG_ERROR, "DHCP: No network interface found.");
        return;
    }

    dhcp_packet_t *pkt = kmalloc(sizeof(dhcp_packet_t));
    memset(pkt, 0, sizeof(dhcp_packet_t));

    pkt->op = 1; // Boot Request
    pkt->htype = 1; pkt->hlen = 6;
    pkt->xid = __builtin_bswap32(dhcp_xid);
    pkt->flags = __builtin_bswap16(0x8000); // Broadcast
    memcpy(pkt->chaddr, network_devices->mac_addr, 6);
    pkt->magic_cookie = __builtin_bswap32(0x63825363);

    uint8_t *opt = pkt->options;
    *opt++ = 53; *opt++ = 1; *opt++ = type;

    if (requested_ip) {
        *opt++ = 50; *opt++ = 4;
        memcpy(opt, &requested_ip, 4); opt += 4;
        
        *opt++ = 54; *opt++ = 4;
        memcpy(opt, &server_ip, 4); opt += 4;
    }

    *opt++ = 255;
    udp_send_packet(0xFFFFFFFF, 68, 67, (uint8_t *)pkt, sizeof(dhcp_packet_t));
    kfree(pkt);
}

void dhcp_client_start() {
    if (dhcp_state != DHCP_STATE_INIT) return;
    if (!network_devices) {
        klog(LOG_WARN, "DHCP: Interface not ready. Retrying soon...");
        return;
    }
    klog(LOG_INFO, "DHCP: DISCOVER started.");
    dhcp_send_packet(1, 0);
    dhcp_state = DHCP_STATE_SELECTING;
}

void dhcp_handle_packet(dhcp_packet_t *pkt, size_t len) {
    (void)len;
    if (pkt->op != 2 || __builtin_bswap32(pkt->xid) != dhcp_xid) return;

    uint8_t type = 0;
    uint8_t *opt = pkt->options;
    while (*opt != 255) {
        if (*opt == 53) type = opt[2];
        else if (*opt == 54) server_ip = *(uint32_t *)(&opt[2]);
        opt += opt[1] + 2;
    }

    if (type == 2 && dhcp_state == DHCP_STATE_SELECTING) { // OFFER
        offered_ip = pkt->yiaddr;
        klog(LOG_INFO, "DHCP: OFFER received (%d.%d.%d.%d). REQUESTING...", (offered_ip>>0)&0xFF, (offered_ip>>8)&0xFF, (offered_ip>>16)&0xFF, (offered_ip>>24)&0xFF);
        dhcp_state = DHCP_STATE_REQUESTING;
        dhcp_send_packet(3, offered_ip);
    } else if (type == 5 && dhcp_state == DHCP_STATE_REQUESTING) { // ACK
        ip_set_local_ip(__builtin_bswap32(pkt->yiaddr));
        dhcp_state = DHCP_STATE_BOUND;
        klog(LOG_INFO, "DHCP: BOUND! Network ready.");
    }
}

static void dhcp_poll_handler(net_dev_t *net_dev, const ipv4_header_t *ip_hdr, udp_header_t *udp_hdr, const uint8_t *data, size_t size) {
    (void)net_dev; (void)ip_hdr; (void)udp_hdr;
    dhcp_handle_packet((dhcp_packet_t*)data, size);
}

void dhcp_init() { 
    udp_register_handler(68, dhcp_poll_handler);
    klog(LOG_INFO, "DHCP initialized and handler registered."); 
}
