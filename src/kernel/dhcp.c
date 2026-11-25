#include "dhcp.h"
#include "udp.h"
#include "net.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "ip.h"
#include "isr.h" // timer_get_ticks

typedef enum {
    DHCP_STATE_INIT,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND
} dhcp_state_t;

static dhcp_state_t dhcp_state  = DHCP_STATE_INIT;
static uint32_t     offered_ip  = 0;
static uint32_t     server_ip   = 0;
static uint32_t     dhcp_xid    = 0x55AA55AA;

/* Retransmission state: DISCOVER/REQUEST are resent until OFFER/ACK
 * arrives; without this a single lost packet leaves the stack without
 * an IP forever. */
static uint64_t dhcp_last_send_ticks = 0;
static int      dhcp_retries = 0;
#define DHCP_RESEND_INTERVAL_TICKS 100 /* 1 s at 100 Hz */
#define DHCP_MAX_RETRIES 5

static void dhcp_send_packet(uint8_t type, uint32_t requested_ip) {
    if (!network_devices) {
        klog(LOG_ERROR, "DHCP: No network interface found.");
        return;
    }
    dhcp_packet_t *pkt = kmalloc(sizeof(dhcp_packet_t));
    if (!pkt) return;
    memset(pkt, 0, sizeof(dhcp_packet_t));

    pkt->op           = 1; /* Boot Request */
    pkt->htype        = 1;
    pkt->hlen         = 6;
    pkt->xid          = __builtin_bswap32(dhcp_xid);
    pkt->flags        = __builtin_bswap16(0x8000); /* Broadcast */
    memcpy(pkt->chaddr, network_devices->mac_addr, 6);
    pkt->magic_cookie = __builtin_bswap32(0x63825363);

    uint8_t *opt = pkt->options;
    *opt++ = 53; *opt++ = 1; *opt++ = type; /* DHCP Message Type */

    if (requested_ip) {
        *opt++ = 50; *opt++ = 4;
        memcpy(opt, &requested_ip, 4); opt += 4;

        *opt++ = 54; *opt++ = 4;
        memcpy(opt, &server_ip, 4); opt += 4;
    }
    *opt++ = 255; /* End */

    udp_send_packet(0xFFFFFFFF, 68, 67, (uint8_t *)pkt, sizeof(dhcp_packet_t));
    kfree(pkt);
}

void dhcp_client_start() {
    if (!network_devices) {
        klog(LOG_WARN, "DHCP: Interface not ready.");
        return;
    }
    klog(LOG_INFO, "DHCP: DISCOVER started.");
    dhcp_retries = 0;
    dhcp_last_send_ticks = timer_get_ticks();
    dhcp_send_packet(1, 0);
    dhcp_state = DHCP_STATE_SELECTING;
}

void dhcp_poll() {
    if (dhcp_state != DHCP_STATE_SELECTING && dhcp_state != DHCP_STATE_REQUESTING)
        return;
    uint64_t now = timer_get_ticks();
    if (now - dhcp_last_send_ticks < DHCP_RESEND_INTERVAL_TICKS) return;
    if (dhcp_retries >= DHCP_MAX_RETRIES) {
        /* Restart the cycle: a fresh xid avoids a stale OFFER being ignored. */
        if (dhcp_retries == DHCP_MAX_RETRIES) {
            klog(LOG_WARN, "DHCP: no response after retries, restarting DISCOVER");
            dhcp_xid++;
            dhcp_retries = 0;
            dhcp_last_send_ticks = now;
            dhcp_send_packet(1, 0);
            dhcp_state = DHCP_STATE_SELECTING;
        }
        return;
    }
    dhcp_retries++;
    dhcp_last_send_ticks = now;
    if (dhcp_state == DHCP_STATE_SELECTING) {
        klog(LOG_INFO, "DHCP: retransmit DISCOVER (%d)", dhcp_retries);
        dhcp_send_packet(1, 0);
    } else {
        klog(LOG_INFO, "DHCP: retransmit REQUEST (%d)", dhcp_retries);
        dhcp_send_packet(3, offered_ip);
    }
}

void dhcp_handle_packet(dhcp_packet_t *pkt, size_t len) {
    if (!pkt || len < offsetof(dhcp_packet_t, options)) return;
    if (pkt->op != 2 || __builtin_bswap32(pkt->xid) != dhcp_xid) return;

    uint8_t  msg_type   = 0;
    uint32_t opt_server = 0;
    uint32_t opt_subnet = 0;
    uint32_t opt_gw     = 0;
    uint32_t opt_dns    = 0;

    size_t opt_max = len - offsetof(dhcp_packet_t, options);
    if (opt_max > sizeof(pkt->options)) opt_max = sizeof(pkt->options);

    size_t pos = 0;
    while (pos < opt_max) {
        uint8_t code = pkt->options[pos++];
        if (code == 255) break; /* End */
        if (code == 0) continue; /* PAD */
        if (pos >= opt_max) break;
        uint8_t olen = pkt->options[pos++];
        if ((size_t)olen > opt_max - pos) break;
        const uint8_t *odata = &pkt->options[pos];
        switch (code) {
            case 53: if (olen >= 1) msg_type = odata[0];           break; /* DHCP Msg Type */
            case 54: if (olen >= 4) memcpy(&opt_server, odata, 4); break; /* Server ID */
            case  1: if (olen >= 4) memcpy(&opt_subnet, odata, 4); break; /* Subnet Mask */
            case  3: if (olen >= 4) memcpy(&opt_gw,     odata, 4); break; /* Router/GW */
            case  6: if (olen >= 4) memcpy(&opt_dns,    odata, 4); break; /* DNS Server */
        }
        pos += olen;
    }

    if (msg_type == 2 && dhcp_state == DHCP_STATE_SELECTING) { /* OFFER */
        offered_ip = pkt->yiaddr;
        server_ip  = opt_server;
        klog(LOG_INFO, "DHCP: OFFER received. Sending REQUEST...");
        dhcp_state = DHCP_STATE_REQUESTING;
        dhcp_retries = 0;
        dhcp_last_send_ticks = timer_get_ticks();
        dhcp_send_packet(3, offered_ip);

    } else if (msg_type == 5 && dhcp_state == DHCP_STATE_REQUESTING) { /* ACK */
        uint32_t bound_ip = __builtin_bswap32(pkt->yiaddr);
        ip_set_local_ip(bound_ip);
        if (opt_subnet) ip_set_subnet_mask(__builtin_bswap32(opt_subnet));
        if (opt_gw)     ip_set_default_gateway(__builtin_bswap32(opt_gw));
        if (opt_dns) {
            extern void dns_init(uint32_t nameserver_ip);
            dns_init(__builtin_bswap32(opt_dns));
        }
        /* Mirror config onto the interface for ifconfig/sysmon */
        if (network_devices) {
            network_devices->ip_addr = bound_ip;
            network_devices->netmask = opt_subnet ? __builtin_bswap32(opt_subnet) : 0;
            network_devices->gateway = opt_gw ? __builtin_bswap32(opt_gw) : 0;
        }
        dhcp_state = DHCP_STATE_BOUND;
        klog(LOG_INFO, "DHCP: BOUND! IP=%d.%d.%d.%d GW set.",
             (bound_ip >> 24) & 0xFF, (bound_ip >> 16) & 0xFF,
             (bound_ip >>  8) & 0xFF,  bound_ip        & 0xFF);
    }
}

static void dhcp_poll_handler(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                               udp_header_t *udp_hdr, const uint8_t *data, size_t size) {
    (void)net_dev; (void)ip_hdr; (void)udp_hdr;
    dhcp_handle_packet((dhcp_packet_t *)data, size);
}

void dhcp_init() {
    udp_register_handler(68, dhcp_poll_handler);
    klog(LOG_INFO, "DHCP initialized.");
}
