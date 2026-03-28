#include "tcp.h"
#include "ip.h"
#include "log.h"
#include "heap.h"
#include "net.h"
#include "event.h"
#include "kstring.h"

tcp_tcb_t* active_tcbs = NULL;
static uint16_t next_local_port = 49152;

typedef struct {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_len;
} __attribute__((packed)) pseudo_hdr_t;

static uint16_t tcp_checksum(const void* data, size_t len, uint32_t src_ip, uint32_t dest_ip) {
    pseudo_hdr_t ph;
    ph.src_ip = __builtin_bswap32(src_ip);
    ph.dest_ip = __builtin_bswap32(dest_ip);
    ph.zero = 0;
    ph.protocol = 0x06;
    ph.tcp_len = __builtin_bswap16((uint16_t)len);

    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)&ph;
    for (size_t i = 0; i < sizeof(pseudo_hdr_t) / 2; i++) sum += ptr[i];

    ptr = (uint16_t*)data;
    for (size_t i = 0; i < len / 2; i++) sum += ptr[i];
    if (len % 2) sum += ((uint8_t*)data)[len - 1];

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void tcp_send_packet(tcp_tcb_t* tcb, uint8_t flags, const void* data, size_t len) {
    size_t tcp_len = sizeof(tcp_header_t) + len;
    uint8_t* buf = kmalloc(tcp_len);
    memset(buf, 0, tcp_len);

    tcp_header_t* hdr = (tcp_header_t*)buf;
    hdr->src_port = __builtin_bswap16(tcb->local_port);
    hdr->dest_port = __builtin_bswap16(tcb->remote_port);
    hdr->seq_num = __builtin_bswap32(tcb->snd_nxt);
    hdr->ack_num = __builtin_bswap32(tcb->rcv_nxt);
    hdr->data_offset_res = (sizeof(tcp_header_t) / 4) << 4;
    hdr->flags = flags;
    hdr->window_size = __builtin_bswap16(8192);
    
    if (len > 0) memcpy(buf + sizeof(tcp_header_t), data, len);

    hdr->checksum = 0;
    hdr->checksum = tcp_checksum(buf, tcp_len, tcb->local_ip, tcb->remote_ip);

    ip_send_packet(network_devices, tcb->remote_ip, 0x06, buf, tcp_len);
    kfree(buf);
}

void tcp_init() {
    active_tcbs = NULL;
    klog(LOG_INFO, "TCP: Real stack initialized.");
}

tcp_tcb_t* tcp_create_tcb() {
    tcp_tcb_t* tcb = (tcp_tcb_t*)kmalloc(sizeof(tcp_tcb_t));
    memset(tcb, 0, sizeof(tcp_tcb_t));
    tcb->state = TCP_STATE_CLOSED;
    
    tcb->recv_buffer_size = 65536;
    tcb->recv_buffer = kmalloc(tcb->recv_buffer_size);
    tcb->recv_data_len = 0;
    tcb->recv_read_idx = 0;

    tcb->next = active_tcbs;
    active_tcbs = tcb;
    return tcb;
}

int tcp_connect_tcb(tcp_tcb_t* tcb, uint32_t remote_ip, uint16_t remote_port) {
    if (!tcb) return -1;
    tcb->remote_ip = remote_ip;
    tcb->remote_port = remote_port;
    tcb->local_ip = ip_get_local_ip();
    tcb->local_port = next_local_port++;
    tcb->snd_nxt = 1000;
    tcb->state = TCP_STATE_SYN_SENT;

    tcp_send_packet(tcb, TCP_FLAG_SYN, NULL, 0);
    tcb->snd_nxt++; 
    
    // Wait for ESTABLISHED
    int timeout = 10000000;
    while (tcb->state != TCP_STATE_ESTABLISHED && timeout > 0) {
        timeout--;
        __asm__("pause");
    }
    
    return (tcb->state == TCP_STATE_ESTABLISHED) ? 0 : -1;
}

int tcp_send_tcb(tcp_tcb_t* tcb, const void* buf, size_t len) {
    if (!tcb || tcb->state != TCP_STATE_ESTABLISHED) return -1;
    tcp_send_packet(tcb, TCP_FLAG_ACK | TCP_FLAG_PSH, buf, len);
    tcb->snd_nxt += len;
    return (int)len;
}

int tcp_recv_tcb(tcp_tcb_t* tcb, void* buf, size_t len) {
    if (!tcb) return -1;
    
    while (tcb->recv_data_len == 0) {
        if (tcb->state == TCP_STATE_CLOSED) return 0;
        __asm__("pause");
    }

    size_t to_copy = (len < tcb->recv_data_len) ? len : tcb->recv_data_len;
    for (size_t i = 0; i < to_copy; i++) {
        ((uint8_t*)buf)[i] = tcb->recv_buffer[tcb->recv_read_idx];
        tcb->recv_read_idx = (tcb->recv_read_idx + 1) % tcb->recv_buffer_size;
    }
    tcb->recv_data_len -= to_copy;
    return (int)to_copy;
}

void tcp_handle_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr, const uint8_t *packet, size_t size) {
    (void)net_dev;
    if (size < sizeof(tcp_header_t)) return;
    tcp_header_t *hdr = (tcp_header_t *)packet;
    uint32_t src_ip = __builtin_bswap32(ip_hdr->src_ip);
    uint16_t src_port = __builtin_bswap16(hdr->src_port);
    uint16_t dest_port = __builtin_bswap16(hdr->dest_port);

    tcp_tcb_t *tcb = active_tcbs;
    while (tcb) {
        if (tcb->remote_ip == src_ip && tcb->remote_port == src_port && tcb->local_port == dest_port) {
            uint32_t seq = __builtin_bswap32(hdr->seq_num);
            uint32_t ack = __builtin_bswap32(hdr->ack_num);

            if (tcb->state == TCP_STATE_SYN_SENT && (hdr->flags & TCP_FLAG_SYN) && (hdr->flags & TCP_FLAG_ACK)) {
                tcb->rcv_nxt = seq + 1;
                tcb->snd_una = ack;
                tcb->state = TCP_STATE_ESTABLISHED;
                tcp_send_packet(tcb, TCP_FLAG_ACK, NULL, 0);
            } else if (tcb->state == TCP_STATE_ESTABLISHED) {
                tcb->snd_una = ack;
                size_t hlen = (hdr->data_offset_res >> 4) * 4;
                size_t payload_len = size - hlen;
                
                if (payload_len > 0) {
                    // Copy to recv_buffer
                    uint8_t* payload = (uint8_t*)packet + hlen;
                    size_t write_idx = (tcb->recv_read_idx + tcb->recv_data_len) % tcb->recv_buffer_size;
                    
                    for (size_t i = 0; i < payload_len; i++) {
                        if (tcb->recv_data_len >= tcb->recv_buffer_size) break; // Buffer full
                        tcb->recv_buffer[write_idx] = payload[i];
                        write_idx = (write_idx + 1) % tcb->recv_buffer_size;
                        tcb->recv_data_len++;
                    }
                    
                    tcb->rcv_nxt += payload_len;
                    tcp_send_packet(tcb, TCP_FLAG_ACK, NULL, 0);
                }
                
                if (hdr->flags & TCP_FLAG_FIN) {
                    tcb->rcv_nxt++;
                    tcp_send_packet(tcb, TCP_FLAG_ACK, NULL, 0);
                    tcb->state = TCP_STATE_CLOSED;
                }
            }
            return;
        }
        tcb = tcb->next;
    }
}

int tcp_close_tcb(tcp_tcb_t* tcb) {
    if (!tcb) return -1;
    tcp_send_packet(tcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    tcb->state = TCP_STATE_CLOSED;
    return 0;
}
