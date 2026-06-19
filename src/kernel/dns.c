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

static void dns_encode_name(const char *hostname, uint8_t *out, size_t *out_len) {
    size_t pos = 0;
    const char *p = hostname;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        uint8_t label_len = (uint8_t)(dot - p);
        out[pos++] = label_len;
        for (uint8_t i = 0; i < label_len; i++) out[pos++] = (uint8_t)p[i];
        p = dot;
        if (*p == '.') p++;
    }
    out[pos++] = 0; /* root label */
    *out_len = pos;
}

static void dns_reply_handler(net_dev_t *dev, const ipv4_header_t *ip_hdr,
                               udp_header_t *udp_hdr,
                               const uint8_t *data, size_t len) {
    (void)dev; (void)ip_hdr; (void)udp_hdr;
    if (len < sizeof(dns_header_t)) return;

    dns_header_t *hdr = (dns_header_t *)data;
    if (__builtin_bswap16(hdr->id) != dns_pending_id) return;
    if (__builtin_bswap16(hdr->ancount) == 0) return;

    /* Skip question section */
    const uint8_t *ptr = data + sizeof(dns_header_t);
    const uint8_t *end = data + len;

    /* Skip QNAME */
    while (ptr < end && *ptr != 0) { ptr += *ptr + 1; }
    ptr++; /* root label */
    ptr += 4; /* QTYPE + QCLASS */

    /* Parse answer RRs */
    uint16_t ancount = __builtin_bswap16(hdr->ancount);
    for (uint16_t i = 0; i < ancount && ptr < end; i++) {
        /* Skip NAME (possibly compressed pointer) */
        if ((*ptr & 0xC0) == 0xC0) { ptr += 2; }
        else { while (ptr < end && *ptr) { ptr += *ptr + 1; } ptr++; }

        if (ptr + 10 > end) break;
        uint16_t rtype  = __builtin_bswap16(*(uint16_t *)ptr); ptr += 2;
        ptr += 2; /* class */
        ptr += 4; /* TTL */
        uint16_t rdlen = __builtin_bswap16(*(uint16_t *)ptr); ptr += 2;

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
    if (!dns_server_ip) {
        klog(LOG_ERROR, "DNS: No nameserver configured.");
        return 0;
    }

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
    dns_encode_name(hostname, qptr, &name_len);
    qptr += name_len;

    *(uint16_t *)qptr = __builtin_bswap16(1); qptr += 2; /* QTYPE A */
    *(uint16_t *)qptr = __builtin_bswap16(1); qptr += 2; /* QCLASS IN */

    size_t total = (size_t)(qptr - buf);
    udp_send_packet(dns_server_ip, DNS_LOCAL_PORT, DNS_PORT, buf, total);

    /* Block until reply */
    int timeout = DNS_TIMEOUT;
    while (!dns_got_reply && timeout-- > 0) {
        waitqueue_add(&dns_wq, get_current_thread());
        get_current_thread()->state = THREAD_BLOCKED;
        schedule();
    }

    if (!dns_got_reply) {
        klog(LOG_WARN, "DNS: Resolve timeout for %s.", hostname);
        return 0;
    }
    klog(LOG_INFO, "DNS: Resolved %s -> %d.%d.%d.%d", hostname,
         (dns_result >> 24) & 0xFF, (dns_result >> 16) & 0xFF,
         (dns_result >>  8) & 0xFF,  dns_result        & 0xFF);
    return dns_result;
}
