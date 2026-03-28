#include "socket.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "net.h"
#include "udp.h" // For UDP protocol handler registration and sending
#include "thread.h" // For get_current_thread, THREAD_BLOCKED, THREAD_READY
#include "scheduler.h" // For schedule
#include "dhcp.h"

socket_t *active_sockets = NULL;

// This function is now the specific handler for UDP data, called by the IP layer
void sock_udp_receive_packet_handler(net_dev_t *net_dev, const ipv4_header_t *ip_hdr, udp_header_t *udp_hdr, const uint8_t *data, size_t len) {
    (void)net_dev;
    (void)ip_hdr;

    // If destination port is DHCP client port (68) or server port (67), it's a DHCP packet
    // This is a temporary hack because the client often sends from 68 and expects replies to 68.
    // However, server replies to 67.
    // This needs to be refined.
    if (__builtin_bswap16(udp_hdr->dest_port) == 68 || __builtin_bswap16(udp_hdr->dest_port) == 67) {
        dhcp_handle_packet((dhcp_packet_t *)data, len);
        return;
    }

    // Find the socket that is bound to the destination port
    socket_t *current_sock = active_sockets;
    while (current_sock) {
        if (current_sock->protocol == IPPROTO_UDP && current_sock->local_addr.sin_port == udp_hdr->dest_port) {
            // Buffer the incoming data
            size_t space_available = current_sock->proto_data.udp_data.recv_buffer_size - current_sock->proto_data.udp_data.recv_data_len;
            if (len > space_available) {
                klog(LOG_WARN, "SOCKET: UDP receive: Buffer overflow, dropping packet.");
                return;
            }
            // Copy data to the end of the circular buffer
            memcpy(current_sock->proto_data.udp_data.recv_buffer + current_sock->proto_data.udp_data.recv_data_len, data, len);
            current_sock->proto_data.udp_data.recv_data_len += len;
            klog(LOG_INFO, "SOCKET: UDP packet received for port %d, len=%d", __builtin_bswap16(udp_hdr->dest_port), len);

            // Wake up any waiting threads
            waitqueue_wakeup_all(&current_sock->waiting_threads);
            return;
        }
        current_sock = current_sock->next;
    }
    
    // If we get here, no listening socket was found for this port.
    // This is common for DHCP replies before the socket is fully managed.
    // klog(LOG_INFO, "SOCKET: UDP packet for unknown port %d dropped.", __builtin_bswap16(udp_hdr->dest_port));
}


void sock_handle_incoming_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr, const uint8_t *payload, size_t payload_size, uint8_t protocol) {
    switch (protocol) {
        case IPPROTO_UDP: {
            // sock_udp_receive_packet_handler expects udp_header_t, not ip_hdr.
            // ip_handle_packet already passed payload starting from udp_header_t.
            udp_header_t *udp_hdr = (udp_header_t *)payload;
            const uint8_t *udp_data = payload + sizeof(udp_header_t);
            size_t udp_data_len = payload_size - sizeof(udp_header_t);
            sock_udp_receive_packet_handler(net_dev, ip_hdr, udp_hdr, udp_data, udp_data_len);
            break;
        }
        case IPPROTO_TCP: {
            klog(LOG_WARN, "SOCKET: TCP packets not yet supported.");
            break;
        }
        default: {
            klog(LOG_WARN, "SOCKET: Unknown protocol %d for incoming packet.", protocol);
            break;
        }
    }
}


socket_t *sock_create(int domain, int type, int protocol) {
    if (domain != AF_INET) {
        klog(LOG_ERROR, "SOCKET: Unsupported domain %d", domain);
        return NULL;
    }
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        klog(LOG_ERROR, "SOCKET: Unsupported type %d", type);
        return NULL;
    }

    if (protocol != IPPROTO_UDP && protocol != IPPROTO_TCP) { // Allow TCP for future, but won't work
        klog(LOG_ERROR, "SOCKET: Unsupported protocol %d", protocol);
        return NULL;
    }

    socket_t *sock = (socket_t *)kmalloc(sizeof(socket_t));
    if (!sock) {
        klog(LOG_ERROR, "SOCKET: Failed to allocate socket_t");
        return NULL;
    }
    memset(sock, 0, sizeof(socket_t));

    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->state = SOCK_STATE_CLOSED;
    waitqueue_init(&sock->waiting_threads);
    
    // Initialize UDP specific data
    if (protocol == IPPROTO_UDP) {
        sock->proto_data.udp_data.recv_buffer_size = 4096; // Default buffer size
        sock->proto_data.udp_data.recv_buffer = (uint8_t*)kmalloc(sock->proto_data.udp_data.recv_buffer_size);
        if (!sock->proto_data.udp_data.recv_buffer) {
            klog(LOG_ERROR, "SOCKET: Failed to allocate recv buffer");
            kfree(sock);
            return NULL;
        }
        sock->proto_data.udp_data.recv_data_len = 0;
        sock->proto_data.udp_data.recv_read_idx = 0;
    }


    // Add to active sockets list
    sock->next = active_sockets;
    active_sockets = sock;

    klog(LOG_INFO, "SOCKET: Created new socket (fd).");
    return sock;
}

// Minimal implementation: for UDP, just set remote address. For TCP, this would initiate handshake.
int sock_connect(socket_t *sock, const sockaddr_in_t *addr) {
    if (!sock || !addr) {
        return -1;
    }

    if (sock->domain != AF_INET || addr->sin_family != AF_INET) {
        klog(LOG_ERROR, "SOCKET: Connect: Mismatched domain or family.");
        return -1;
    }
    
    // For UDP, simply store the remote address
    if (sock->protocol == IPPROTO_UDP) {
        memcpy(&sock->remote_addr, addr, sizeof(sockaddr_in_t));
        sock->state = SOCK_STATE_CONNECTED;
        klog(LOG_INFO, "SOCKET: UDP socket connected (remote addr set).");
        return 0;
    }
    // TODO: Implement TCP handshake
    klog(LOG_WARN, "SOCKET: Connect: Only UDP is supported.");
    return -1;
}


int sock_send(socket_t *sock, const void *buf, size_t len, int flags) {
    if (!sock || !buf || len == 0) {
        return -1;
    }
    (void)flags;

    if (sock->protocol == IPPROTO_UDP) {
        if (sock->state != SOCK_STATE_CONNECTED) {
            klog(LOG_ERROR, "SOCKET: UDP send failed: not connected.");
            return -1;
        }

        if (!network_devices) {
            klog(LOG_ERROR, "SOCKET: No network device available for sending.");
            return -1;
        }
        // Send UDP packet using the network stack
        udp_send_packet(sock->remote_addr.sin_addr, sock->local_addr.sin_port, sock->remote_addr.sin_port, (const uint8_t*)buf, len);
        klog(LOG_INFO, "SOCKET: UDP packet sent, len=%d", len);
        return len;
    }
    // TODO: Implement TCP send
    klog(LOG_WARN, "SOCKET: Send: Only UDP is supported.");
    return -1;
}


int sock_recv(socket_t *sock, void *buf, size_t len, int flags) {
    if (!sock || !buf || len == 0) {
        return -1;
    }
    (void)flags;

    if (sock->protocol == IPPROTO_UDP) {
        if (sock->state == SOCK_STATE_CLOSED) {
            klog(LOG_ERROR, "SOCKET: UDP recv failed: socket closed.");
            return -1;
        }

        // Wait for data if buffer is empty
        while (sock->proto_data.udp_data.recv_data_len == 0) {
            // This is a blocking call. Put current thread to sleep.
            waitqueue_add(&sock->waiting_threads, get_current_thread());
            get_current_thread()->state = THREAD_BLOCKED;
            schedule(); // Yield CPU until woken up by interrupt handler
        }

        size_t bytes_to_copy = len;
        if (bytes_to_copy > sock->proto_data.udp_data.recv_data_len) {
            bytes_to_copy = sock->proto_data.udp_data.recv_data_len;
        }
        memcpy(buf, sock->proto_data.udp_data.recv_buffer + sock->proto_data.udp_data.recv_read_idx, bytes_to_copy);
        sock->proto_data.udp_data.recv_read_idx += bytes_to_copy;
        sock->proto_data.udp_data.recv_data_len -= bytes_to_copy;

        // Reset buffer pointers if buffer is empty
        if (sock->proto_data.udp_data.recv_data_len == 0) {
            sock->proto_data.udp_data.recv_read_idx = 0;
        }
        klog(LOG_INFO, "SOCKET: UDP recv: %d bytes.", bytes_to_copy);
        return bytes_to_copy;
    }
    // TODO: Implement TCP recv
    klog(LOG_WARN, "SOCKET: Recv: Only UDP is supported.");
    return -1;
}

int sock_close(socket_t *sock) {
    if (!sock) {
        return -1;
    }
    // Remove from active sockets list
    socket_t **current = &active_sockets;
    while (*current) {
        if (*current == sock) {
            *current = sock->next;
            break;
        }
        current = &(*current)->next;
    }

    if (sock->protocol == IPPROTO_UDP && sock->proto_data.udp_data.recv_buffer) {
        kfree(sock->proto_data.udp_data.recv_buffer);
    }
    kfree(sock);
    klog(LOG_INFO, "SOCKET: Closed socket.");
    return 0;
}

// Minimal implementation: bind UDP socket to a local port
int sock_bind(socket_t *sock, const sockaddr_in_t *addr) {
    if (!sock || !addr) {
        return -1;
    }

    if (sock->protocol == IPPROTO_UDP) {
        // Ensure port is not already in use
        socket_t *current_sock = active_sockets;
        while (current_sock) {
            if (current_sock != sock && current_sock->protocol == IPPROTO_UDP && current_sock->local_addr.sin_port == addr->sin_port) {
                klog(LOG_ERROR, "SOCKET: Bind failed: Port %d already in use.", __builtin_bswap16(addr->sin_port));
                return -1; // Port already in use
            }
            current_sock = current_sock->next;
        }
        memcpy(&sock->local_addr, addr, sizeof(sockaddr_in_t));
        sock->state = SOCK_STATE_BOUND;
        // Register the receive handler for this port with the UDP layer
        udp_register_handler(__builtin_bswap16(addr->sin_port), sock_udp_receive_packet_handler);
        klog(LOG_INFO, "SOCKET: UDP socket bound to port %d", __builtin_bswap16(addr->sin_port));
        return 0;
    }
    klog(LOG_WARN, "SOCKET: Bind: Only UDP is supported for now.");
    return -1;
}

// Not implemented for UDP
int sock_listen(socket_t *sock, int backlog) {
    (void)sock;
    (void)backlog;
    klog(LOG_WARN, "SOCKET: Listen not supported for UDP.");
    return -1;
}
socket_t* sock_accept(socket_t* sock, sockaddr_in_t* addr) {
    (void)sock;
    (void)addr;
    klog(LOG_WARN, "SOCKET: Accept not supported for UDP.");
    return NULL;
}