#include "socket.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "net.h"
#include "udp.h"
#include "tcp.h"
#include "thread.h"
#include "scheduler.h"
#include "dhcp.h"

socket_t *active_sockets = NULL;

/* ── UDP receive handler (called from UDP layer) ─────────────────────────── */
void sock_udp_receive_packet_handler(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                                     udp_header_t *udp_hdr, const uint8_t *data, size_t len) {
    (void)net_dev; (void)ip_hdr;

    uint16_t dst_port = __builtin_bswap16(udp_hdr->dest_port);

    /* DHCP client port — hand off directly */
    if (dst_port == 68 || dst_port == 67) {
        dhcp_handle_packet((dhcp_packet_t *)data, len);
        return;
    }

    socket_t *sock = active_sockets;
    while (sock) {
        if (sock->protocol == IPPROTO_UDP &&
            sock->local_addr.sin_port == udp_hdr->dest_port) {
            size_t space = sock->proto_data.udp_data.recv_buffer_size
                         - sock->proto_data.udp_data.recv_data_len;
            if (len > space) {
                klog(LOG_WARN, "SOCKET: UDP recv buffer overflow, dropping.");
                return;
            }
            memcpy(sock->proto_data.udp_data.recv_buffer
                   + sock->proto_data.udp_data.recv_data_len, data, len);
            sock->proto_data.udp_data.recv_data_len += len;
            waitqueue_wakeup_all(&sock->waiting_threads);
            return;
        }
        sock = sock->next;
    }
}

/* ── Dispatcher called from IP layer ─────────────────────────────────────── */
void sock_handle_incoming_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                                 const uint8_t *payload, size_t payload_size,
                                 uint8_t protocol) {
    switch (protocol) {
        case IPPROTO_UDP: {
            udp_header_t  *udp_hdr  = (udp_header_t *)payload;
            const uint8_t *udp_data = payload + sizeof(udp_header_t);
            size_t         udp_len  = payload_size - sizeof(udp_header_t);
            sock_udp_receive_packet_handler(net_dev, ip_hdr, udp_hdr, udp_data, udp_len);
            break;
        }
        case IPPROTO_TCP:
            tcp_handle_packet(net_dev, ip_hdr, payload, payload_size);
            break;
        default:
            klog(LOG_WARN, "SOCKET: Unknown protocol %d.", protocol);
            break;
    }
}

/* ── sock_create ─────────────────────────────────────────────────────────── */
socket_t *sock_create(int domain, int type, int protocol) {
    if (domain != AF_INET) {
        klog(LOG_ERROR, "SOCKET: Unsupported domain %d", domain);
        return NULL;
    }
    if ((type != SOCK_STREAM && type != SOCK_DGRAM) ||
        (protocol != IPPROTO_UDP && protocol != IPPROTO_TCP)) {
        klog(LOG_ERROR, "SOCKET: Unsupported type/protocol %d/%d", type, protocol);
        return NULL;
    }

    socket_t *sock = (socket_t *)kmalloc(sizeof(socket_t));
    if (!sock) return NULL;
    memset(sock, 0, sizeof(socket_t));

    sock->domain   = domain;
    sock->type     = type;
    sock->protocol = protocol;
    sock->state    = SOCK_STATE_CLOSED;
    waitqueue_init(&sock->waiting_threads);

    if (protocol == IPPROTO_UDP) {
        sock->proto_data.udp_data.recv_buffer_size = 4096;
        sock->proto_data.udp_data.recv_buffer =
            (uint8_t *)kmalloc(sock->proto_data.udp_data.recv_buffer_size);
        if (!sock->proto_data.udp_data.recv_buffer) { kfree(sock); return NULL; }
        sock->proto_data.udp_data.recv_data_len = 0;
        sock->proto_data.udp_data.recv_read_idx = 0;
    } else if (protocol == IPPROTO_TCP) {
        sock->proto_data.tcp_tcb = tcp_create_tcb();
        if (!sock->proto_data.tcp_tcb) { kfree(sock); return NULL; }
    }

    sock->next    = active_sockets;
    active_sockets = sock;
    klog(LOG_INFO, "SOCKET: Created (%s).", protocol == IPPROTO_UDP ? "UDP" : "TCP");
    return sock;
}

/* ── sock_bind ───────────────────────────────────────────────────────────── */
int sock_bind(socket_t *sock, const sockaddr_in_t *addr) {
    if (!sock || !addr) return -1;

    if (sock->protocol == IPPROTO_UDP) {
        socket_t *s = active_sockets;
        while (s) {
            if (s != sock && s->protocol == IPPROTO_UDP &&
                s->local_addr.sin_port == addr->sin_port) {
                klog(LOG_ERROR, "SOCKET: Port already in use.");
                return -1;
            }
            s = s->next;
        }
        memcpy(&sock->local_addr, addr, sizeof(sockaddr_in_t));
        sock->state = SOCK_STATE_BOUND;
        udp_register_handler(__builtin_bswap16(addr->sin_port),
                             sock_udp_receive_packet_handler);
        klog(LOG_INFO, "SOCKET: UDP bound to port %d.", __builtin_bswap16(addr->sin_port));
        return 0;
    }
    /* TCP bind */
    memcpy(&sock->local_addr, addr, sizeof(sockaddr_in_t));
    sock->state = SOCK_STATE_BOUND;
    return 0;
}

/* ── sock_connect ────────────────────────────────────────────────────────── */
int sock_connect(socket_t *sock, const sockaddr_in_t *addr) {
    if (!sock || !addr) return -1;

    if (sock->protocol == IPPROTO_UDP) {
        memcpy(&sock->remote_addr, addr, sizeof(sockaddr_in_t));
        sock->state = SOCK_STATE_CONNECTED;
        return 0;
    }
    if (sock->protocol == IPPROTO_TCP) {
        tcp_tcb_t *tcb = sock->proto_data.tcp_tcb;
        int ret = tcp_connect_tcb(tcb, addr->sin_addr, __builtin_bswap16(addr->sin_port));
        if (ret == 0) {
            memcpy(&sock->remote_addr, addr, sizeof(sockaddr_in_t));
            sock->state = SOCK_STATE_CONNECTED;
            klog(LOG_INFO, "SOCKET: TCP connected.");
        } else {
            klog(LOG_ERROR, "SOCKET: TCP connect failed.");
        }
        return ret;
    }
    return -1;
}

/* ── sock_send ───────────────────────────────────────────────────────────── */
int sock_send(socket_t *sock, const void *buf, size_t len, int flags) {
    if (!sock || !buf || len == 0) return -1;
    (void)flags;

    if (sock->protocol == IPPROTO_UDP) {
        if (sock->state != SOCK_STATE_CONNECTED) return -1;
        if (!network_devices) return -1;
        udp_send_packet(sock->remote_addr.sin_addr,
                        sock->local_addr.sin_port,
                        sock->remote_addr.sin_port,
                        (const uint8_t *)buf, len);
        return (int)len;
    }
    if (sock->protocol == IPPROTO_TCP) {
        return tcp_send_tcb(sock->proto_data.tcp_tcb, buf, len);
    }
    return -1;
}

/* ── sock_recv ───────────────────────────────────────────────────────────── */
int sock_recv(socket_t *sock, void *buf, size_t len, int flags) {
    if (!sock || !buf || len == 0) return -1;
    (void)flags;

    if (sock->protocol == IPPROTO_UDP) {
        while (sock->proto_data.udp_data.recv_data_len == 0) {
            waitqueue_add(&sock->waiting_threads, get_current_thread());
            get_current_thread()->state = THREAD_BLOCKED;
            schedule();
        }
        size_t to_copy = len < sock->proto_data.udp_data.recv_data_len
                         ? len : sock->proto_data.udp_data.recv_data_len;
        memcpy(buf,
               sock->proto_data.udp_data.recv_buffer
               + sock->proto_data.udp_data.recv_read_idx,
               to_copy);
        sock->proto_data.udp_data.recv_read_idx  += to_copy;
        sock->proto_data.udp_data.recv_data_len  -= to_copy;
        if (sock->proto_data.udp_data.recv_data_len == 0)
            sock->proto_data.udp_data.recv_read_idx = 0;
        return (int)to_copy;
    }
    if (sock->protocol == IPPROTO_TCP) {
        return tcp_recv_tcb(sock->proto_data.tcp_tcb, buf, len);
    }
    return -1;
}

/* ── sock_listen / sock_accept (TCP server — stub, not needed for client) ── */
int sock_listen(socket_t *sock, int backlog) {
    (void)sock; (void)backlog;
    klog(LOG_WARN, "SOCKET: listen() not yet implemented.");
    return -1;
}

socket_t *sock_accept(socket_t *sock, sockaddr_in_t *addr) {
    (void)sock; (void)addr;
    klog(LOG_WARN, "SOCKET: accept() not yet implemented.");
    return NULL;
}

/* ── sock_close ──────────────────────────────────────────────────────────── */
int sock_close(socket_t *sock) {
    if (!sock) return -1;

    socket_t **cur = &active_sockets;
    while (*cur) {
        if (*cur == sock) { *cur = sock->next; break; }
        cur = &(*cur)->next;
    }

    if (sock->protocol == IPPROTO_UDP && sock->proto_data.udp_data.recv_buffer)
        kfree(sock->proto_data.udp_data.recv_buffer);

    if (sock->protocol == IPPROTO_TCP && sock->proto_data.tcp_tcb)
        tcp_close_tcb(sock->proto_data.tcp_tcb);

    kfree(sock);
    klog(LOG_INFO, "SOCKET: Closed.");
    return 0;
}
