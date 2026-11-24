#include "dns.h"
#include "udp.h"
#include "net.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "thread.h"
#include "scheduler.h"
#include "waitqueue.h"

#define DNS_PORT       53
#define DNS_LOCAL_PORT 5300
#define DNS_TIMEOUT    5000000

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

static uint32_t  dns_server_ip   = 0;
static uint32_t  dns_result      = 0;
static uint16_t  dns_txid        = 0xAB01;
static uint16_t  dns_pending_id  = 0;
static wait_queue_t dns_wq;
static int       dns_got_reply   = 0;

static int dns_encode_name(const char *hostname, uint8_t *out, size_t out_cap, size_t *out_len) {
    size_t pos = 0;
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        size_t label_len = (size_t)(dot - p);
        if (label_len == 0 || label_len > 63) return -1;
        if (pos + 1 + label_len + 1 > out_cap) return -1;
        out[pos++] = (uint8_t)label_len;
        for (size_t i = 0; i < label_len; i++) out[pos++] = (uint8_t)p[i];
        p = dot;
        if (*p == '.') p++;
    }
    if (pos + 1 > out_cap) return -1;
    out[pos++] = 0; /* root label */
    *out_len = pos;
    return 0;
}

static void dns_reply_handler(net_dev_t *dev, const ipv4_header_t *ip_hdr,
                               udp_header_t *udp_hdr,
                               const uint8_t *data, size_t len) {
    (void)dev; (void)ip_hdr; (void)udp_hdr;
    if (!data || len < sizeof(dns_header_t)) return;

    dns_header_t *hdr = (dns_header_t *)data;
    if (__builtin_bswap16(hdr->id) != dns_pending_id) return;
    if (__builtin_bswap16(hdr->ancount) == 0) return;

    /* Skip question section */
    const uint8_t *ptr = data + sizeof(dns_header_t);
    const uint8_t *end = data + len;

    /* Skip QNAME */
    while (ptr < end && *ptr != 0) {
        if ((*ptr & 0xC0) == 0xC0) {
            if (ptr + 2 > end) return;
            ptr += 2;
            goto qname_done;
        }
        uint8_t l = *ptr;
        if ((size_t)(end - ptr) < (size_t)l + 1) return;
        ptr += l + 1;
    }
    if (ptr >= end) return;
    ptr++; /* root label */
qname_done:
    if ((size_t)(end - ptr) < 4) return;
    ptr += 4; /* QTYPE + QCLASS */

    /* Parse answer RRs */
    uint16_t ancount = __builtin_bswap16(hdr->ancount);
    for (uint16_t i = 0; i < ancount && ptr < end; i++) {
        /* Skip NAME (possibly compressed pointer) */
        while (ptr < end && *ptr != 0) {
            if ((*ptr & 0xC0) == 0xC0) {
                if (ptr + 2 > end) return;
                ptr += 2;
                goto rr_name_done;
            }
            uint8_t l = *ptr;
            if ((size_t)(end - ptr) < (size_t)l + 1) return;
            ptr += l + 1;
        }
        if (ptr >= end) return;
        ptr++; /* root label */
rr_name_done:
        if ((size_t)(end - ptr) < 10) break;
        uint16_t rtype = (uint16_t)(((uint16_t)ptr[0] << 8) | ptr[1]);
        ptr += 8; /* type(2) + class(2) + TTL(4) */
        uint16_t rdlen = (uint16_t)(((uint16_t)ptr[0] << 8) | ptr[1]);
        ptr += 2;

        if ((size_t)(end - ptr) < rdlen) break;

        if (rtype == 1 && rdlen == 4) { /* A record */
            uint32_t ip;
            memcpy(&ip, ptr, 4);
            dns_result    = __builtin_bswap32(ip);
            dns_got_reply = 1;
            waitqueue_wakeup_all(&dns_wq);
            return;
        }
        ptr += rdlen;
    }
}

void dns_init(uint32_t nameserver_ip) {
    dns_server_ip = nameserver_ip;
    waitqueue_init(&dns_wq);
    udp_register_handler(DNS_LOCAL_PORT, dns_reply_handler);
    klog(LOG_INFO, "DNS: Initialized.");
}

uint32_t dns_resolve(const char *hostname) {
    if (!hostname || !*hostname) return 0;

    uint32_t servers[3];
    int server_count = 0;
    if (dns_server_ip != 0) {
        servers[server_count++] = dns_server_ip;
    }
    // Google Public DNS (8.8.8.8)
    uint32_t google_dns = (8 << 24) | (8 << 16) | (8 << 8) | 8;
    if (dns_server_ip != google_dns) {
        servers[server_count++] = google_dns;
    }
    // Cloudflare Public DNS (1.1.1.1)
    uint32_t cf_dns = (1 << 24) | (1 << 16) | (1 << 8) | 1;
    if (dns_server_ip != cf_dns) {
        servers[server_count++] = cf_dns;
    }

    for (int s = 0; s < server_count; s++) {
        uint32_t current_server = servers[s];

        /* Build DNS query */
        uint8_t  buf[512];
        memset(buf, 0, sizeof(buf));

        dns_pending_id = dns_txid++;
        dns_got_reply  = 0;
        dns_result     = 0;

        dns_header_t *hdr = (dns_header_t *)buf;
        hdr->id      = __builtin_bswap16(dns_pending_id);
        hdr->flags   = __builtin_bswap16(0x0100); /* RD=1 */
        hdr->qdcount = __builtin_bswap16(1);

        size_t   name_len = 0;
        uint8_t *qptr     = buf + sizeof(dns_header_t);
        if (dns_encode_name(hostname, qptr, sizeof(buf) - sizeof(dns_header_t) - 4, &name_len) != 0) {
            klog(LOG_WARN, "DNS: Hostname too long or invalid: %s", hostname);
            return 0;
        }
        qptr += name_len;

        *(uint16_t *)qptr = __builtin_bswap16(1); qptr += 2; /* QTYPE A */
        *(uint16_t *)qptr = __builtin_bswap16(1); qptr += 2; /* QCLASS IN */

        size_t total = (size_t)(qptr - buf);
        udp_send_packet(current_server, DNS_LOCAL_PORT, DNS_PORT, buf, total);

        /* Wait for reply with a deadline */
        uint64_t deadline_ticks = timer_get_ticks() + (DNS_TIMEOUT / 20000);
        while (!dns_got_reply) {
            if (timer_get_ticks() >= deadline_ticks) break;
            extern void net_poll(void);
            net_poll();
            schedule();
        }

        if (dns_got_reply) {
            klog(LOG_INFO, "DNS: Resolved %s -> %d.%d.%d.%d via server %d.%d.%d.%d", hostname,
                 (dns_result >> 24) & 0xFF, (dns_result >> 16) & 0xFF,
                 (dns_result >>  8) & 0xFF,  dns_result        & 0xFF,
                 (current_server >> 24) & 0xFF, (current_server >> 16) & 0xFF,
                 (current_server >>  8) & 0xFF,  current_server        & 0xFF);
            return dns_result;
        }
    }

    klog(LOG_WARN, "DNS: Resolve timeout for %s after trying all nameservers.", hostname);
    return 0;
}
