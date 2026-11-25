#include "socket.h"
#include "errno.h"
#include "heap.h"
#include "isr.h" // timer_get_ticks
#include "ip.h"
#include "kstring.h"
#include "log.h"
#include "net.h"
#include "udp.h"
#include "tcp.h"
#include "thread.h"
#include "scheduler.h"
#include "spinlock.h"
#include "dhcp.h"

socket_t *active_sockets = NULL;
static spinlock_t sock_lock = {0};

static int sock_should_abort(void) {
    thread_t *cur = get_current_thread();
    if (!cur) return 0;
    if (cur->state == THREAD_DEAD) return 1;
    if (cur->sig_pending & ((1U << SIGINT) | (1U << SIGKILL) | (1U << SIGTERM))) return 1;
    return 0;
}

static size_t sock_dgram_consume_locked(socket_t *sock, void *buf, size_t len) {
    size_t avail = sock->proto_data.udp_data.recv_data_len;
    size_t to_copy = len < avail ? len : avail;
    memcpy(buf,
           sock->proto_data.udp_data.recv_buffer
           + sock->proto_data.udp_data.recv_read_idx,
           to_copy);
    sock->proto_data.udp_data.recv_read_idx += to_copy;
    sock->proto_data.udp_data.recv_data_len -= to_copy;
    if (sock->proto_data.udp_data.recv_data_len > 0) {
        memmove(sock->proto_data.udp_data.recv_buffer,
                sock->proto_data.udp_data.recv_buffer
                + sock->proto_data.udp_data.recv_read_idx,
                sock->proto_data.udp_data.recv_data_len);
    }
    sock->proto_data.udp_data.recv_read_idx = 0;
    return to_copy;
}

/* Full network service tick from blocking receive paths: drain NIC RX
 * rings and drive ARP/DHCP retransmissions, so a ping (or any datagram
 * wait) makes progress even when every other thread is blocked. */
static void sockets_poll_nics(void) {
    extern void net_poll(void);
    net_poll();
}

/* ── UDP receive handler (called from UDP layer) ─────────────────────────── */
void sock_udp_receive_packet_handler(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                                     udp_header_t *udp_hdr, const uint8_t *data, size_t len) {
    (void)net_dev;

    uint16_t dst_port = __builtin_bswap16(udp_hdr->dest_port);

    /* DHCP client port — hand off directly */
    if (dst_port == 68 || dst_port == 67) {
        dhcp_handle_packet((dhcp_packet_t *)data, len);
        return;
    }

    /* Hand off to kernel UDP protocol handlers (e.g. DNS client on port 5300) */
    udp_handle_packet(net_dev, ip_hdr, (const uint8_t *)udp_hdr, len + sizeof(udp_header_t));

    uint64_t iflags = spinlock_acquire_irqsave(&sock_lock);
    socket_t *sock = active_sockets;
    while (sock) {
        if (sock->protocol == IPPROTO_UDP &&
            sock->local_addr.sin_port == udp_hdr->dest_port) {
            size_t data_len = sock->proto_data.udp_data.recv_data_len;
            size_t buf_size = sock->proto_data.udp_data.recv_buffer_size;
            size_t space = (buf_size > data_len) ? (buf_size - data_len) : 0;
            if (len > space) {
                sock->rx_dropped_ctr++;
                spinlock_release_irqrestore(&sock_lock, iflags);
                klog(LOG_WARN, "SOCKET: UDP recv buffer overflow, dropping.");
                return;
            }
            /* Record datagram source for recvfrom() */
            if (ip_hdr)
                sock->recv_src_ip = __builtin_bswap32(ip_hdr->src_ip);
            sock->recv_src_port = __builtin_bswap16(udp_hdr->src_port);
            if (len > 0) {
                memcpy(sock->proto_data.udp_data.recv_buffer + data_len, data, len);
                sock->proto_data.udp_data.recv_data_len = data_len + len;
            }
            spinlock_release_irqrestore(&sock_lock, iflags);
            waitqueue_wakeup_all(&sock->waiting_threads);
            return;
        }
        sock = sock->next;
    }
    spinlock_release_irqrestore(&sock_lock, iflags);
}

/* ── RAW (ICMP) delivery: route echo replies to raw ICMP sockets ─────────── */
void sock_raw_icmp_receive(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                           const uint8_t *data, size_t len) {
    (void)net_dev;
    uint64_t iflags = spinlock_acquire_irqsave(&sock_lock);
    socket_t *sock = active_sockets;
    while (sock) {
        if (sock->type == SOCK_RAW && sock->protocol == IPPROTO_ICMP) {
            size_t data_len = sock->proto_data.udp_data.recv_data_len;
            size_t buf_size = sock->proto_data.udp_data.recv_buffer_size;
            size_t space = (buf_size > data_len) ? (buf_size - data_len) : 0;
            if (len > space) {
                sock = sock->next;
                continue;
            }
            sock->recv_src_ip = __builtin_bswap32(ip_hdr ? ip_hdr->src_ip : 0);
            sock->recv_src_port = 0;
            if (len > 0) {
                memcpy(sock->proto_data.udp_data.recv_buffer + data_len, data, len);
                sock->proto_data.udp_data.recv_data_len = data_len + len;
            }
            waitqueue_wakeup_all(&sock->waiting_threads);
        }
        sock = sock->next;
    }
    spinlock_release_irqrestore(&sock_lock, iflags);
}

/* ── Dispatcher called from IP layer ─────────────────────────────────────── */
void sock_handle_incoming_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                                 const uint8_t *payload, size_t payload_size,
                                 uint8_t protocol) {
    switch (protocol) {
        case IPPROTO_UDP: {
            if (payload_size < sizeof(udp_header_t)) return;
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
    int is_udp = (type == SOCK_DGRAM && (protocol == IPPROTO_UDP || protocol == 0));
    int is_tcp = (type == SOCK_STREAM && (protocol == IPPROTO_TCP || protocol == 0));
    int is_raw = (type == SOCK_RAW && protocol == IPPROTO_ICMP);
    if (!is_udp && !is_tcp && !is_raw) {
        klog(LOG_ERROR, "SOCKET: Unsupported type/protocol %d/%d", type, protocol);
        return NULL;
    }

    socket_t *sock = (socket_t *)kmalloc(sizeof(socket_t));
    if (!sock) return NULL;
    memset(sock, 0, sizeof(socket_t));

    sock->domain   = domain;
    sock->type     = type;
    sock->protocol = is_tcp ? IPPROTO_TCP : (is_udp ? IPPROTO_UDP : protocol);
    sock->state    = SOCK_STATE_CREATED;
    waitqueue_init(&sock->waiting_threads);

    if (is_udp || is_raw) {
        /* Datagram buffer shared by UDP and RAW (ICMP) sockets */
        sock->proto_data.udp_data.recv_buffer_size = 4096;
        sock->proto_data.udp_data.recv_buffer =
            (uint8_t *)kmalloc(sock->proto_data.udp_data.recv_buffer_size);
        if (!sock->proto_data.udp_data.recv_buffer) { kfree(sock); return NULL; }
        sock->proto_data.udp_data.recv_data_len = 0;
        sock->proto_data.udp_data.recv_read_idx = 0;
    } else if (is_tcp) {
        sock->proto_data.tcp_tcb = tcp_create_tcb();
        if (!sock->proto_data.tcp_tcb) { kfree(sock); return NULL; }
    }

    uint64_t iflags = spinlock_acquire_irqsave(&sock_lock);
    sock->next    = active_sockets;
    active_sockets = sock;
    spinlock_release_irqrestore(&sock_lock, iflags);
    klog(LOG_INFO, "SOCKET: Created (%s).",
         is_tcp ? "TCP" : (is_raw ? "RAW/ICMP" : "UDP"));
    return sock;
}

/* ── sock_bind ───────────────────────────────────────────────────────────── */
int sock_bind(socket_t *sock, const sockaddr_in_t *addr) {
    if (!sock || !addr) return -1;

    if (sock->protocol == IPPROTO_UDP) {
        uint64_t iflags = spinlock_acquire_irqsave(&sock_lock);
        socket_t *s = active_sockets;
        while (s) {
            if (s != sock && s->protocol == IPPROTO_UDP &&
                s->local_addr.sin_port == addr->sin_port) {
                spinlock_release_irqrestore(&sock_lock, iflags);
                klog(LOG_ERROR, "SOCKET: Port already in use.");
                return -1;
            }
            s = s->next;
        }
        memcpy(&sock->local_addr, addr, sizeof(sockaddr_in_t));
        sock->state = SOCK_STATE_BOUND;
        spinlock_release_irqrestore(&sock_lock, iflags);
        udp_register_handler(__builtin_bswap16(addr->sin_port),
                             sock_udp_receive_packet_handler);
        klog(LOG_INFO, "SOCKET: UDP bound to port %d.", __builtin_bswap16(addr->sin_port));
        return 0;
    }
    /* TCP / RAW bind */
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
    /* RAW (ICMP): record default destination, like unconnected UDP */
    memcpy(&sock->remote_addr, addr, sizeof(sockaddr_in_t));
    sock->state = SOCK_STATE_CONNECTED;
    return 0;
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
        for (;;) {
            if (sock->state == SOCK_STATE_CLOSED) return -EBADF;
            if (sock_should_abort()) return -EINTR;
            uint64_t iflags = spinlock_acquire_irqsave(&sock_lock);
            if (sock->proto_data.udp_data.recv_data_len != 0) {
                size_t to_copy = sock_dgram_consume_locked(sock, buf, len);
                spinlock_release_irqrestore(&sock_lock, iflags);
                return (int)to_copy;
            }
            spinlock_release_irqrestore(&sock_lock, iflags);

            sockets_poll_nics();
            if (sock_should_abort()) return -EINTR;
            if (sock->proto_data.udp_data.recv_data_len != 0) continue;

            waitqueue_add(&sock->waiting_threads, get_current_thread());
            if (sock->proto_data.udp_data.recv_data_len != 0 || sock_should_abort()) {
                get_current_thread()->state = THREAD_READY;
                continue;
            }
            get_current_thread()->state = THREAD_BLOCKED;
            schedule();
        }
    }
    if (sock->protocol == IPPROTO_TCP) {
        return tcp_recv_tcb(sock->proto_data.tcp_tcb, buf, len);
    }
    return -1;
}

/* ── Datagram send with explicit destination (sendto) ─────────────────── */
int sock_sendto(socket_t *sock, const void *buf, size_t len, int flags,
                const sockaddr_in_t *dest_addr) {
    if (!sock || !buf || len == 0) return -EINVAL;
    (void)flags;

    if (sock->protocol == IPPROTO_UDP) {
        uint32_t dest = dest_addr ? dest_addr->sin_addr : sock->remote_addr.sin_addr;
        uint16_t dport = dest_addr ? dest_addr->sin_port : sock->remote_addr.sin_port;
        if (dest == 0) return -EDESTADDRREQ;
        if (!network_devices) return -ENETDOWN;
        udp_send_packet(dest,
                        sock->local_addr.sin_port,
                        dport,
                        (const uint8_t *)buf, len);
        return (int)len;
    }
    if (sock->type == SOCK_RAW && sock->protocol == IPPROTO_ICMP) {
        uint32_t dest = dest_addr ? dest_addr->sin_addr : sock->remote_addr.sin_addr;
        if (dest == 0) return -EDESTADDRREQ;
        if (!network_devices) return -ENETDOWN;
        /* ping builds the full ICMP message (header + checksum) itself;
         * raw ICMP send is an IP datagram pass-through */
        ip_send_packet(network_devices, dest, IP_PROTOCOL_ICMP,
                       (const uint8_t *)buf, len);
        return (int)len;
    }
    if (sock->protocol == IPPROTO_TCP) return sock_send(sock, buf, len, flags);
    return -EINVAL;
}

/* ── Datagram receive with source (recvfrom) ───────────────────────────── */
int sock_recvfrom(socket_t *sock, void *buf, size_t len, int flags,
                  sockaddr_in_t *src_addr, socklen_t *addrlen) {
    if (!sock || !buf || len == 0) return -EINVAL;
    (void)flags;

    if (sock->type == SOCK_RAW || sock->protocol == IPPROTO_UDP) {
        uint64_t deadline_ticks = 0;
        if (sock->recv_timeout_ms)
            deadline_ticks = timer_get_ticks() + sock->recv_timeout_ms / 10;
        for (;;) {
            if (sock->state == SOCK_STATE_CLOSED) return -EBADF;
            if (sock_should_abort()) return -EINTR;
            uint64_t iflags = spinlock_acquire_irqsave(&sock_lock);
            if (sock->proto_data.udp_data.recv_data_len != 0) {
                size_t to_copy = sock_dgram_consume_locked(sock, buf, len);
                uint32_t src_ip = sock->recv_src_ip;
                uint16_t src_port = sock->recv_src_port;
                spinlock_release_irqrestore(&sock_lock, iflags);

                if (src_addr) {
                    memset(src_addr, 0, sizeof(sockaddr_in_t));
                    src_addr->sin_family = AF_INET;
                    src_addr->sin_port   = __builtin_bswap16(src_port);
                    src_addr->sin_addr   = src_ip;
                }
                if (addrlen) *addrlen = sizeof(sockaddr_in_t);
                return (int)to_copy;
            }
            spinlock_release_irqrestore(&sock_lock, iflags);

            sockets_poll_nics(); /* deliver pending packets before waiting */
            if (sock_should_abort()) return -EINTR;
            if (sock->proto_data.udp_data.recv_data_len != 0) continue;

            if (deadline_ticks) {
                /* With a timeout we cannot block permanently: a blocked
                   thread is never woken to re-check its deadline (no
                   timer-based wakeup). Yield instead, staying runnable.
                   MUST mark ourselves READY: schedule() only scans for
                   READY threads, a RUNNING thread would starve. */
                if (timer_get_ticks() >= deadline_ticks) return -EAGAIN;
                get_current_thread()->state = THREAD_READY;
                schedule();
            } else {
                /* No timeout: proper block until packet/shutdown wake */
                waitqueue_add(&sock->waiting_threads, get_current_thread());
                if (sock->proto_data.udp_data.recv_data_len != 0 || sock_should_abort()) {
                    get_current_thread()->state = THREAD_READY;
                    continue;
                }
                get_current_thread()->state = THREAD_BLOCKED;
                schedule();
            }
        }
    }
    if (sock->protocol == IPPROTO_TCP) return sock_recv(sock, buf, len, flags);
    return -EINVAL;
}

/* ── sock_shutdown ─────────────────────────────────────────────────────── */
int sock_shutdown(socket_t *sock, int how) {
    if (!sock) return -EBADF;
    if (how != 0 && how != 1 && how != 2) return -EINVAL;
    if (sock->protocol == IPPROTO_TCP && sock->proto_data.tcp_tcb)
        tcp_close_tcb(sock->proto_data.tcp_tcb);
    sock->state = SOCK_STATE_CLOSED;
    waitqueue_wakeup_all(&sock->waiting_threads);
    return 0;
}

/* ── sock_getsockname ─────────────────────────────────────────────────── */
int sock_getsockname(socket_t *sock, sockaddr_in_t *addr, socklen_t *addrlen) {
    if (!sock || !addr) return -EBADF;
    memcpy(addr, &sock->local_addr, sizeof(sockaddr_in_t));
    if (addrlen) *addrlen = sizeof(sockaddr_in_t);
    return 0;
}

/* ── sock_setsockopt ─────────────────────────────────────────────────── */
int sock_setsockopt(socket_t *sock, int level, int optname,
                    const void *optval, socklen_t optlen) {
    if (!sock) return -EBADF;
    if (level != SOL_SOCKET) return -ENOPROTOOPT;
    if (optlen < sizeof(struct timeval) || !optval) return -EINVAL;
    const struct timeval *tv = (const struct timeval *)optval;
    uint32_t ms = (uint32_t)tv->tv_sec * 1000 + (uint32_t)tv->tv_usec / 1000;
    switch (optname) {
        case SO_RCVTIMEO: sock->recv_timeout_ms = ms; return 0;
        case SO_SNDTIMEO: sock->snd_timeout_ms = ms; return 0;
        default: return -ENOPROTOOPT;
    }
}

/* ── sock_listen / sock_accept (TCP server) ─────────────────────────────── */
int sock_listen(socket_t *sock, int backlog) {
    if (!sock || sock->protocol != IPPROTO_TCP) return -1;
    (void)backlog;
    sock->state = SOCK_STATE_LISTENING;
    sock->backlog_count = 0;
    klog(LOG_INFO, "SOCKET: TCP listening on port %d.", __builtin_bswap16(sock->local_addr.sin_port));
    return 0;
}

socket_t *sock_accept(socket_t *sock, sockaddr_in_t *addr) {
    if (!sock || sock->protocol != IPPROTO_TCP || sock->state != SOCK_STATE_LISTENING) {
        return NULL;
    }

    for (;;) {
        if (sock->state != SOCK_STATE_LISTENING || sock_should_abort()) return NULL;
        uint64_t iflags = spinlock_acquire_irqsave(&sock_lock);
        if (sock->backlog_count > 0) {
            socket_t *client_sock = sock->backlog_queue[0];
            for (int i = 0; i < sock->backlog_count - 1; i++) {
                sock->backlog_queue[i] = sock->backlog_queue[i + 1];
            }
            sock->backlog_count--;
            spinlock_release_irqrestore(&sock_lock, iflags);

            if (addr) {
                memcpy(addr, &client_sock->remote_addr, sizeof(sockaddr_in_t));
            }
            return client_sock;
        }
        spinlock_release_irqrestore(&sock_lock, iflags);

        sockets_poll_nics();
        if (sock_should_abort()) return NULL;
        if (sock->backlog_count > 0) continue;

        waitqueue_add(&sock->waiting_threads, get_current_thread());
        if (sock->backlog_count > 0 || sock_should_abort() || sock->state != SOCK_STATE_LISTENING) {
            get_current_thread()->state = THREAD_READY;
            continue;
        }
        get_current_thread()->state = THREAD_BLOCKED;
        schedule();
    }
}

/* ── sock_close ──────────────────────────────────────────────────────────── */
int sock_close(socket_t *sock) {
    if (!sock) return -1;

    uint64_t iflags = spinlock_acquire_irqsave(&sock_lock);
    socket_t **cur = &active_sockets;
    while (*cur) {
        if (*cur == sock) { *cur = sock->next; break; }
        cur = &(*cur)->next;
    }
    socket_t *pending_children[16];
    int pending_count = 0;
    if (sock->state == SOCK_STATE_LISTENING && sock->backlog_count > 0) {
        pending_count = sock->backlog_count;
        if (pending_count > 16) pending_count = 16;
        for (int i = 0; i < pending_count; i++) {
            pending_children[i] = sock->backlog_queue[i];
            sock->backlog_queue[i] = NULL;
        }
        sock->backlog_count = 0;
    }
    sock->state = SOCK_STATE_CLOSED;
    spinlock_release_irqrestore(&sock_lock, iflags);

    waitqueue_wakeup_all(&sock->waiting_threads);

    for (int i = 0; i < pending_count; i++) {
        if (pending_children[i]) {
            sock_close(pending_children[i]);
        }
    }

    if ((sock->protocol == IPPROTO_UDP ||
         (sock->type == SOCK_RAW && sock->protocol == IPPROTO_ICMP)) &&
        sock->proto_data.udp_data.recv_buffer)
        kfree(sock->proto_data.udp_data.recv_buffer);

    if (sock->protocol == IPPROTO_TCP && sock->proto_data.tcp_tcb)
        tcp_close_tcb(sock->proto_data.tcp_tcb);

    kfree(sock);
    klog(LOG_INFO, "SOCKET: Closed.");
    return 0;
}
