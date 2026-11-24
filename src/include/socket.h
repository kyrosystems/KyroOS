#ifndef SOCKET_H
#define SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include "net.h" // For net_dev_t, IP addresses
#include "udp.h" // For UDP functions
#include "thread.h" // For blocking/non-blocking behavior

// Socket domains
#define AF_INET     2   // IPv4 Internet protocols

// Socket types
#define SOCK_STREAM 1   // Sequenced, reliable, connection-based byte streams (TCP)
#define SOCK_DGRAM  2   // Connectionless, unreliable datagrams (UDP)
// (SOCK_RAW defined below with protocols)

// Protocols
#define IPPROTO_IP      0   // Internet Protocol
#define IPPROTO_ICMP    1   // Control Message Protocol
#define IPPROTO_TCP     6   // Transmission Control Protocol
#define IPPROTO_UDP     17  // User Datagram Protocol
#define SOCK_RAW      3

#define SOL_SOCKET    1
#define SO_RCVTIMEO   20
#define SO_SNDTIMEO   21

#define IPPROTO_ICMP  1

typedef uint32_t socklen_t;

struct timeval {
    uint32_t tv_sec;
    uint32_t tv_usec;
};

// Socket states
typedef enum {
    SOCK_STATE_CREATED,
    SOCK_STATE_BOUND,
    SOCK_STATE_CONNECTED, // For TCP
    SOCK_STATE_LISTENING, // For TCP
    SOCK_STATE_CLOSED
} socket_state_t;

// Forward declaration for generic socket_t in fd_entry_t
struct socket;

// Address structure (IPv4)
typedef struct sockaddr_in {
    uint16_t sin_family; // Address family (AF_INET)
    uint16_t sin_port;   // Port number (in network byte order)
    uint32_t sin_addr;   // IP address (in network byte order)
    uint8_t  sin_zero[8]; // Zero this for sockaddr_in compatibility
} __attribute__((packed)) sockaddr_in_t;

#include "tcp.h" // For tcp_tcb_t

#include "waitqueue.h"

// Generic socket structure
typedef struct socket {
    int domain;
    int type;
    int protocol;
    socket_state_t state;

    // Local address/port
    sockaddr_in_t local_addr;
    // Remote address/port (for connected sockets)
    sockaddr_in_t remote_addr;

    net_dev_t* net_dev; // Associated network device

    union {
        struct { // For UDP sockets
            // Buffers for send/receive
            uint8_t* recv_buffer;
            size_t recv_buffer_size;
            volatile size_t recv_data_len; // Amount of data currently in buffer
            volatile size_t recv_read_idx; // Read pointer
        } udp_data;
        tcp_tcb_t* tcp_tcb; // For TCP sockets
    } proto_data;
    
    // For blocking calls (common to both UDP and TCP recv)
    wait_queue_t waiting_threads;

    // For listening server sockets
    struct socket *backlog_queue[16];
    int backlog_count;

    // Socket options + datagram source tracking (host-readable IP order)
    uint32_t recv_timeout_ms; // 0 = block forever (SO_RCVTIMEO)
    uint32_t snd_timeout_ms;  // 0 = block forever (SO_SNDTIMEO)
    uint32_t recv_src_ip;     // source of last received datagram
    uint16_t recv_src_port;
    uint64_t rx_dropped_ctr;  // datagrams dropped (buffer overflow)

    // Functions for protocol-specific operations
    int (*sock_connect)(struct socket* sock, const sockaddr_in_t* addr);
    int (*sock_send)(struct socket* sock, const void* buf, size_t len, int flags);
    int (*sock_recv)(struct socket* sock, void* buf, size_t len, int flags);
    void (*sock_close)(struct socket* sock);

    struct socket* next; // For linked list of all active sockets
} socket_t;




// Global list of active sockets (managed by kernel)
extern socket_t* active_sockets;

// Functions for socket management (kernel-side)
socket_t* sock_create(int domain, int type, int protocol);
int sock_bind(socket_t* sock, const sockaddr_in_t* addr);
int sock_listen(socket_t* sock, int backlog);
socket_t* sock_accept(socket_t* sock, sockaddr_in_t* addr);
int sock_connect(socket_t* sock, const sockaddr_in_t* addr);
int sock_send(socket_t* sock, const void* buf, size_t len, int flags);
int sock_recv(socket_t* sock, void* buf, size_t len, int flags);
int sock_close(socket_t* sock);
int sock_sendto(socket_t* sock, const void* buf, size_t len, int flags,
                const sockaddr_in_t* dest_addr);
int sock_recvfrom(socket_t* sock, void* buf, size_t len, int flags,
                  sockaddr_in_t* src_addr, socklen_t* addrlen);
int sock_shutdown(socket_t* sock, int how);
int sock_getsockname(socket_t* sock, sockaddr_in_t* addr, socklen_t* addrlen);
int sock_setsockopt(socket_t* sock, int level, int optname,
                    const void* optval, socklen_t optlen);
void sock_raw_icmp_receive(net_dev_t *net_dev, const ipv4_header_t *ip_hdr,
                           const uint8_t *data, size_t len);
void sock_handle_incoming_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr, const uint8_t *payload, size_t payload_size, uint8_t protocol);

#endif // SOCKET_H
