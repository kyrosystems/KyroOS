#include "tcp.h"
#include "socket.h"
#include "ip.h"
#include "isr.h"
#include "log.h"
#include "heap.h"
#include "net.h"
#include "kstring.h"
#include "scheduler.h"
#include "thread.h"

tcp_tcb_t *active_tcbs       = NULL;
static uint16_t next_local_port = 49152;

typedef struct {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_len;
} __attribute__((packed)) pseudo_hdr_t;

static uint16_t tcp_checksum(const void *data, size_t len,
                              uint32_t src_ip, uint32_t dest_ip) {
    pseudo_hdr_t ph;
    ph.src_ip    = __builtin_bswap32(src_ip);
    ph.dest_ip   = __builtin_bswap32(dest_ip);
    ph.zero      = 0;
    ph.protocol  = 0x06;
    ph.tcp_len   = __builtin_bswap16((uint16_t)len);

    uint32_t  sum = 0;
    uint16_t *ptr = (uint16_t *)&ph;
    for (size_t i = 0; i < sizeof(pseudo_hdr_t) / 2; i++) sum += ptr[i];

    ptr = (uint16_t *)data;
    for (size_t i = 0; i < len / 2; i++) sum += ptr[i];
    if (len % 2) sum += (uint32_t)((uint8_t *)data)[len - 1] << 8; // odd byte is high 

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void tcp_send_packet(tcp_tcb_t *tcb, uint8_t flags,
                            const void *data, size_t len) {
    size_t   tcp_len = sizeof(tcp_header_t) + len;
    uint8_t *buf     = kmalloc(tcp_len);
    memset(buf, 0, tcp_len);

    tcp_header_t *hdr = (tcp_header_t *)buf;
    hdr->src_port          = __builtin_bswap16(tcb->local_port);
    hdr->dest_port         = __builtin_bswap16(tcb->remote_port);
    hdr->seq_num           = __builtin_bswap32(tcb->snd_nxt);
    hdr->ack_num           = __builtin_bswap32(tcb->rcv_nxt);
    hdr->data_offset_res   = (sizeof(tcp_header_t) / 4) << 4;
    hdr->flags             = flags;
    hdr->window_size       = __builtin_bswap16(8192);

    if (len > 0) memcpy(buf + sizeof(tcp_header_t), data, len);

    hdr->checksum = 0;
    hdr->checksum = tcp_checksum(buf, tcp_len, tcb->local_ip, tcb->remote_ip);

    ip_send_packet(network_devices, tcb->remote_ip, 0x06, buf, tcp_len);
    kfree(buf);
}

void tcp_init() {
    active_tcbs = NULL;
    klog(LOG_INFO, "TCP: Stack initialized.");
}

tcp_tcb_t *tcp_create_tcb() {
    tcp_tcb_t *tcb = (tcp_tcb_t *)kmalloc(sizeof(tcp_tcb_t));
    memset(tcb, 0, sizeof(tcp_tcb_t));
    tcb->state            = TCP_STATE_CLOSED;
    tcb->recv_buffer_size = 65536;
    tcb->recv_buffer      = kmalloc(tcb->recv_buffer_size);
    tcb->recv_data_len    = 0;
    tcb->recv_read_idx    = 0;
    waitqueue_init(&tcb->connect_wq);
    waitqueue_init(&tcb->recv_wq);
    tcb->next   = active_tcbs;
    active_tcbs = tcb;
    return tcb;
}

int tcp_connect_tcb(tcp_tcb_t *tcb, uint32_t remote_ip, uint16_t remote_port) {
    if (!tcb) return -1;
    tcb->remote_ip   = remote_ip;
    tcb->remote_port = remote_port;
    tcb->local_ip    = ip_get_local_ip();
    tcb->local_port  = next_local_port++;
    uint32_t isn     = 1000;
    tcb->snd_nxt     = isn;
    tcb->state       = TCP_STATE_SYN_SENT;

    tcp_send_packet(tcb, TCP_FLAG_SYN, NULL, 0);
    tcb->snd_nxt = isn + 1;


    extern void net_poll(void);
    uint64_t deadline  = timer_get_ticks() + 1500; // 15 s @ 100 Hz 
    uint64_t next_retx = timer_get_ticks() + 30;   // 300 ms 
    while (tcb->state == TCP_STATE_SYN_SENT && timer_get_ticks() < deadline) {
        net_poll();
        if (tcb->state != TCP_STATE_SYN_SENT) break;
        if (timer_get_ticks() >= next_retx) {
            uint32_t save = tcb->snd_nxt;
            tcb->snd_nxt  = isn;
            tcp_send_packet(tcb, TCP_FLAG_SYN, NULL, 0);
            tcb->snd_nxt  = save;
            next_retx = timer_get_ticks() + 30;
        }
        get_current_thread()->state = THREAD_READY;
        schedule();
    }
    if (tcb->state == TCP_STATE_SYN_SENT) tcb->state = TCP_STATE_CLOSED; // timed out 
    return (tcb->state == TCP_STATE_ESTABLISHED) ? 0 : -1;
}

int tcp_send_tcb(tcp_tcb_t *tcb, const void *buf, size_t len) {
    if (!tcb || tcb->state != TCP_STATE_ESTABLISHED) return -1;
    tcp_send_packet(tcb, TCP_FLAG_ACK | TCP_FLAG_PSH, buf, len);
    tcb->snd_nxt += len;
    return (int)len;
}

int tcp_recv_tcb(tcp_tcb_t *tcb, void *buf, size_t len) {
    if (!tcb) return -1;

    while (tcb->recv_data_len == 0) {
        if (tcb->state == TCP_STATE_CLOSED || tcb->state == TCP_STATE_CLOSE_WAIT)
            return 0;
        extern void net_poll(void);
        net_poll();
        get_current_thread()->state = THREAD_READY;
        schedule();
    }

    size_t to_copy = (len < tcb->recv_data_len) ? len : tcb->recv_data_len;
    for (size_t i = 0; i < to_copy; i++) {
        ((uint8_t *)buf)[i]  = tcb->recv_buffer[tcb->recv_read_idx];
        tcb->recv_read_idx   = (tcb->recv_read_idx + 1) % tcb->recv_buffer_size;
    }
    tcb->recv_data_len -= to_copy;
    return (int)to_copy;
}

void tcp_handle_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                       const uint8_t *packet, size_t size) {
    (void)net_dev;
    if (size < sizeof(tcp_header_t)) return;

    tcp_header_t *hdr      = (tcp_header_t *)packet;
    uint32_t      src_ip   = __builtin_bswap32(ip_hdr->src_ip);
    uint16_t      src_port = __builtin_bswap16(hdr->src_port);
    uint16_t      dst_port = __builtin_bswap16(hdr->dest_port);

    tcp_tcb_t *tcb = active_tcbs;
    while (tcb) {
        if (tcb->remote_ip   == src_ip   &&
            tcb->remote_port == src_port &&
            tcb->local_port  == dst_port) {

            uint32_t seq = __builtin_bswap32(hdr->seq_num);
            uint32_t ack = __builtin_bswap32(hdr->ack_num);

            if (tcb->state == TCP_STATE_SYN_SENT &&
                (hdr->flags & TCP_FLAG_SYN) && (hdr->flags & TCP_FLAG_ACK)) {
                tcb->rcv_nxt = seq + 1;
                tcb->snd_una = ack;
                tcb->state   = TCP_STATE_ESTABLISHED;
                tcp_send_packet(tcb, TCP_FLAG_ACK, NULL, 0);
                /* Wake up tcp_connect_tcb */
                waitqueue_wakeup_all(&tcb->connect_wq);

            } else if (tcb->state == TCP_STATE_ESTABLISHED) {
                tcb->snd_una = ack;
                size_t hlen        = (hdr->data_offset_res >> 4) * 4;
                size_t payload_len = size - hlen;

                if (payload_len > 0) {
                    uint8_t *payload   = (uint8_t *)packet + hlen;
                    size_t   write_idx = (tcb->recv_read_idx + tcb->recv_data_len)
                                         % tcb->recv_buffer_size;
                    for (size_t i = 0; i < payload_len; i++) {
                        if (tcb->recv_data_len >= tcb->recv_buffer_size) break;
                        tcb->recv_buffer[write_idx] = payload[i];
                        write_idx = (write_idx + 1) % tcb->recv_buffer_size;
                        tcb->recv_data_len++;
                    }
                    tcb->rcv_nxt += payload_len;
                    tcp_send_packet(tcb, TCP_FLAG_ACK, NULL, 0);
                    /* Wake up tcp_recv_tcb */
                    waitqueue_wakeup_all(&tcb->recv_wq);
                }

                if (hdr->flags & TCP_FLAG_FIN) {
                    tcb->rcv_nxt++;
                    tcp_send_packet(tcb, TCP_FLAG_ACK, NULL, 0);
                    tcb->state = TCP_STATE_CLOSE_WAIT;
                    /* Let caller notice via recv returning 0 */
                    waitqueue_wakeup_all(&tcb->recv_wq);
                }

                if (hdr->flags & TCP_FLAG_RST) {
                    tcb->state = TCP_STATE_CLOSED;
                    waitqueue_wakeup_all(&tcb->recv_wq);
                    waitqueue_wakeup_all(&tcb->connect_wq);
                }
            }
            return;
        }
        tcb = tcb->next;
    }

    // Handle incoming SYN for listening server sockets
    if (!tcb && (hdr->flags & TCP_FLAG_SYN) && !(hdr->flags & TCP_FLAG_ACK)) {
        extern socket_t *active_sockets;
        socket_t *lsock = active_sockets;
        while (lsock) {
            if (lsock->protocol == IPPROTO_TCP &&
                lsock->state == SOCK_STATE_LISTENING &&
                __builtin_bswap16(lsock->local_addr.sin_port) == dst_port) {
                break;
            }
            lsock = lsock->next;
        }

        if (lsock) {
            uint32_t seq = __builtin_bswap32(hdr->seq_num);
            tcp_tcb_t *child_tcb = tcp_create_tcb();
            if (child_tcb) {
                child_tcb->local_ip = __builtin_bswap32(ip_hdr->dest_ip);
                child_tcb->local_port = dst_port;
                child_tcb->remote_ip = src_ip;
                child_tcb->remote_port = src_port;
                child_tcb->rcv_nxt = seq + 1;
                uint32_t isn = 2000 + (seq ^ 0x5A5A);
                child_tcb->snd_nxt = isn;
                child_tcb->state = TCP_STATE_ESTABLISHED;

                tcp_send_packet(child_tcb, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                child_tcb->snd_nxt++;

                socket_t *client_sock = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (client_sock) {
                    client_sock->state = SOCK_STATE_CONNECTED;
                    client_sock->proto_data.tcp_tcb = child_tcb;
                    client_sock->local_addr = lsock->local_addr;
                    client_sock->remote_addr.sin_family = AF_INET;
                    client_sock->remote_addr.sin_port = hdr->src_port;
                    client_sock->remote_addr.sin_addr = ip_hdr->src_ip;

                    if (lsock->backlog_count < 16) {
                        lsock->backlog_queue[lsock->backlog_count++] = client_sock;
                        waitqueue_wakeup_all(&lsock->waiting_threads);
                    }
                }
            }
            return;
        }
    }
}

int tcp_close_tcb(tcp_tcb_t *tcb) {
    if (!tcb) return -1;
    if (tcb->state == TCP_STATE_ESTABLISHED || tcb->state == TCP_STATE_CLOSE_WAIT) {
        tcp_send_packet(tcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        tcb->snd_nxt++;
    }
    tcb->state = TCP_STATE_CLOSED;
    waitqueue_wakeup_all(&tcb->recv_wq);
    waitqueue_wakeup_all(&tcb->connect_wq);

    // Unlink from active_tcbs
    tcp_tcb_t **curr = &active_tcbs;
    while (*curr) {
        if (*curr == tcb) {
            *curr = tcb->next;
            break;
        }
        curr = &(*curr)->next;
    }

    if (tcb->recv_buffer) {
        kfree(tcb->recv_buffer);
        tcb->recv_buffer = NULL;
    }
    kfree(tcb);
    return 0;
}
