#include <kyroolib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h> 

uint32_t dns_resolve(const char *host);


#define PING_DEFAULT_COUNT   4
#define PING_DEFAULT_TIMEOUT 2000   
#define PING_PAYLOAD_SIZE    56

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    uint8_t  payload[PING_PAYLOAD_SIZE];
} __attribute__((packed)) icmp_echo_t;

static uint16_t icmp_checksum(const void *data, size_t len) {
    const uint16_t *p   = (const uint16_t *)data;
    uint32_t        sum = 0;
    while (len > 1)  { sum += *p++; len -= 2; }
    if (len)          sum += *(const uint8_t *)p;
    while (sum >> 16) sum  = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint32_t resolve(const char *host) {
    uint32_t ip = 0;
    uint32_t parts[4] = {0}; 
    int n = 0; 
    uint32_t cur = 0;
    bool ok = true;
    bool has_digits = false;

    for (const char *p = host; ; p++) {
        if (*p >= '0' && *p <= '9') { 
            cur = cur * 10 + (*p - '0'); 
            has_digits = true;
            if (cur > 255) { ok = false; break; } 
        }
        else if (*p == '.' || *p == '\0') {
            if (!has_digits || n >= 4) { ok = false; break; }
            parts[n++] = cur; 
            cur = 0; 
            has_digits = false;
            if (*p == '\0') break;
        } else { 
            ok = false; 
            break; 
        }
    }
    
    if (ok && n == 4) {
        ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    } else {
        ip = dns_resolve(host);
    }
    return ip;
}

static void print_ip(uint32_t ip) {
    char buf[20];
    sprintf(buf, "%u.%u.%u.%u",
        (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    print(buf);
}

int main(int argc, char **argv) {
    int         count   = PING_DEFAULT_COUNT;
    const char *host    = NULL;
    bool        flood   = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print("Usage: ping [-c count] [-f] <host|ip>\n");
            return 0;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
            if (count <= 0) count = 1;
        } else if (strcmp(argv[i], "-f") == 0) {
            flood = true;
        } else if (argv[i][0] != '-') {
            host = argv[i];
        }
    }

    if (!host) { 
        print("ping: No host specified. Try --help.\n"); 
        return 1; 
    }

    uint32_t dst_ip = resolve(host);
    if (!dst_ip) {
        print("ping: Cannot resolve host: "); print(host); print("\n");
        return 1;
    }

    print("PING "); print(host); print(" ("); print_ip(dst_ip); print("): ");
    print("64 bytes of data.\n");

    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        print("ping: Failed to open raw socket (SOCK_RAW/ICMP).\n");
        return 1;
    }

    struct timeval tv;
    tv.tv_sec = PING_DEFAULT_TIMEOUT / 1000;
    tv.tv_usec = (PING_DEFAULT_TIMEOUT % 1000) * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port   = 0;
    addr.sin_addr   = dst_ip;

    // psuedo-random identifier for ICMP Echo Request
    uint16_t pid  = (uint16_t)(uintptr_t)&pid; 
    int      sent = 0, received = 0;
    uint32_t rtt_min = 0xFFFFFFFF, rtt_max = 0, rtt_sum = 0;

    for (int seq = 0; seq < count; seq++) {
        // pause 1 second between pings unless in flood mode
        if (seq > 0 && !flood) {
            sleep_ms(1000); 
        }

        icmp_echo_t pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type     = 8; // ICMP Echo Request
        pkt.code     = 0;
        pkt.id       = htons(pid);
        pkt.seq      = htons((uint16_t)seq);
        
        for (int i = 0; i < PING_PAYLOAD_SIZE; i++) pkt.payload[i] = (uint8_t)i;
        pkt.checksum = icmp_checksum(&pkt, sizeof(pkt));

        uint32_t t_start = uptime_ms(); 
        int n = sendto(sockfd, &pkt, sizeof(pkt), 0,
               &addr, sizeof(addr));
        if (n < 0) { 
            print("ping: sendto failed.\n"); 
            continue; 
        }
        sent++;

        // buffer for receiving the reply (IP header + ICMP)
        char rx_buf[sizeof(icmp_echo_t) + 40]; 
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        
        int r = recvfrom(sockfd, rx_buf, sizeof(rx_buf), 0,
                        &from, &fromlen);
        uint32_t rtt = uptime_ms() - t_start;

        // minimum ICMP Echo Reply size is 8 bytes (type, code, checksum, id, seq)
        if (r >= 28) {
            
            int ip_hlen = (rx_buf[0] & 0x0F) * 4;
            icmp_echo_t *reply = (icmp_echo_t *)(rx_buf + ip_hlen);

            if (reply->type == 0 && reply->id == htons(pid) && reply->seq == htons((uint16_t)seq)) {
                received++;
                if (rtt < rtt_min) rtt_min = rtt;
                if (rtt > rtt_max) rtt_max = rtt;
                rtt_sum += rtt;

                if (!flood) {
                    char buf[128];
                    sprintf(buf, "64 bytes from ");
                    print(buf);
                    print_ip(from.sin_addr);
                    sprintf(buf, ": icmp_seq=%d time=%u ms\n", seq, rtt);
                    print(buf);
                } else {
                    print(".");
                }
                continue; // successful reply, go to next iteration
            }
        }

        // If we reach here, it means we didn't get a valid reply
        if (!flood) {
            char buf[64];
            sprintf(buf, "Request timeout for icmp_seq %d\n", seq);
            print(buf);
        } else { 
            print("X"); 
        }
    }

    if (flood) print("\n");

    print("\n--- "); print(host); print(" ping statistics ---\n");
    char buf[128];
    int  loss = sent ? (sent - received) * 100 / sent : 100;
    sprintf(buf, "%d packets transmitted, %d received, %d%% packet loss\n",
            sent, received, loss);
    print(buf);
    if (received > 0) {
        sprintf(buf, "rtt min/avg/max = %u/%u/%u ms\n",
                rtt_min, rtt_sum / received, rtt_max);
        print(buf);
    }

    close(sockfd);
    return (received > 0) ? 0 : 1;
}