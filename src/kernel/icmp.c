#include "icmp.h"
#include "ip.h"
#include "net.h"
#include "log.h"
#include "heap.h"
#include "kstring.h"
#include "isr.h"

static uint32_t last_reply_ip = 0;
static uint64_t last_reply_time = 0;
static bool waiting_for_reply = false;

static uint16_t icmp_checksum(const void* data, size_t len) {
    const uint16_t* u16_buf = (const uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *u16_buf++; len -= 2; }
    if (len == 1) sum += *(const uint8_t*)u16_buf;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void icmp_init() {
    ip_register_protocol_handler(IP_PROTOCOL_ICMP, icmp_handle_packet);
    klog(LOG_INFO, "ICMP initialized.");
}

void icmp_handle_packet(net_dev_t* net_dev, const ipv4_header_t* ip_hdr, const uint8_t* payload, size_t payload_size) {
    if (payload_size < sizeof(icmp_header_t)) return;
    const icmp_header_t* icmp_hdr = (const icmp_header_t*)payload;
    
    if (icmp_checksum(payload, payload_size) != 0) return;
    
    switch (icmp_hdr->type) {
        case ICMP_ECHO_REQUEST: {
            size_t reply_size = payload_size;
            uint8_t* buf = (uint8_t*)kmalloc(reply_size);
            if (!buf) return;
            memcpy(buf, payload, payload_size);
            icmp_header_t* reply = (icmp_header_t*)buf;
            reply->type = ICMP_ECHO_REPLY;
            reply->checksum = 0;
            reply->checksum = icmp_checksum(buf, reply_size);
            ip_send_packet(net_dev, __builtin_bswap32(ip_hdr->src_ip), IP_PROTOCOL_ICMP, buf, reply_size);
            kfree(buf);
            break;
        }
        case ICMP_ECHO_REPLY: {
            last_reply_ip = __builtin_bswap32(ip_hdr->src_ip);
            last_reply_time = timer_get_ticks();
            waiting_for_reply = false;
            klog(LOG_INFO, "ICMP: Ping reply from %d.%d.%d.%d", (last_reply_ip>>24)&0xFF, (last_reply_ip>>16)&0xFF, (last_reply_ip>>8)&0xFF, last_reply_ip&0xFF);
            break;
        }
    }
}

bool icmp_ping(uint32_t dest_ip, uint32_t timeout_ms) {
    if (!network_devices) return false;
    
    waiting_for_reply = true;
    uint8_t data[] = "KyroOS Ping Test";
    icmp_send_echo_request(network_devices, dest_ip, 0x1234, 1, data, sizeof(data));
    
    uint64_t start = timer_get_ticks();
    while (waiting_for_reply && (timer_get_ticks() - start) < (timeout_ms / 10)) {
        if (network_devices->receive_packet) network_devices->receive_packet(network_devices, NULL, 0);
        __asm__("pause");
    }
    
    return !waiting_for_reply;
}

void icmp_send_echo_request(net_dev_t* net_dev, uint32_t dest_ip, uint16_t id, uint16_t sequence, const uint8_t* data, size_t data_size) {
    size_t size = sizeof(icmp_header_t) + data_size;
    uint8_t* buf = (uint8_t*)kmalloc(size);
    if (!buf) return;
    memset(buf, 0, size);
    icmp_header_t* hdr = (icmp_header_t*)buf;
    hdr->type = ICMP_ECHO_REQUEST;
    hdr->id = __builtin_bswap16(id);
    hdr->sequence = __builtin_bswap16(sequence);
    memcpy(buf + sizeof(icmp_header_t), data, data_size);
    hdr->checksum = 0;
    hdr->checksum = icmp_checksum(buf, size);
    ip_send_packet(net_dev, dest_ip, IP_PROTOCOL_ICMP, buf, size);
    kfree(buf);
}
