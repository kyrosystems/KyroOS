#ifndef TCP_H
#define TCP_H

#include <stdint.h>
#include <stddef.h>
#include "ip.h" 
#include "net.h" 
#include "thread.h" 

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20
#define TCP_FLAG_ECE 0x40
#define TCP_FLAG_CWR 0x80

typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset_res; 
    uint8_t  flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
} __attribute__((packed)) tcp_header_t;

typedef enum {
    TCP_STATE_CLOSED,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} tcp_state_t;

typedef struct tcp_tcb {
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;

    uint32_t snd_nxt; 
    uint32_t rcv_nxt; 
    uint32_t snd_una;

    tcp_state_t state;

    uint8_t* recv_buffer;
    size_t recv_buffer_size;
    volatile size_t recv_data_len;
    volatile size_t recv_read_idx;

    struct tcp_tcb* next;
} tcp_tcb_t;

extern tcp_tcb_t* active_tcbs;

void tcp_init();
tcp_tcb_t* tcp_create_tcb();
int tcp_connect_tcb(tcp_tcb_t* tcb, uint32_t remote_ip, uint16_t remote_port);
int tcp_send_tcb(tcp_tcb_t* tcb, const void* buf, size_t len);
int tcp_recv_tcb(tcp_tcb_t* tcb, void* buf, size_t len);
void tcp_handle_packet(net_dev_t *net_dev, const ipv4_header_t *ip_hdr, const uint8_t *packet, size_t size);
int tcp_close_tcb(tcp_tcb_t* tcb);

#endif
