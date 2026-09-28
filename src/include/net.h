#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>
#include "driver.h" // For device_t

// Forward declaration
struct net_dev;

// Generic network device operations
typedef void (*net_send_packet_t)(struct net_dev* dev, const uint8_t* buffer, size_t size);
typedef void (*net_receive_packet_t)(struct net_dev* dev, uint8_t* buffer, size_t size); // For polled receive

// Network device flags (ifconfig/sysmon report these)
#define IFF_UP       0x01
#define IFF_LOOPBACK 0x02
#define IFF_RUNNING  0x04

// Network device structure
typedef struct net_dev {
    device_t* dev; // Pointer to the underlying generic device
    uint8_t mac_addr[6];
    net_send_packet_t send_packet;
    net_receive_packet_t receive_packet;
    // For zero-copy: a ring buffer or similar mechanism for received packets

    // IPv4 configuration (host-readable order: 0xAABBCCDD = A.B.C.D).
    // Set by DHCP or static config; 0 = unconfigured.
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;

    uint32_t mtu;    // 0 = driver does not report MTU
    uint32_t flags;  // IFF_* bitmask

    // Real driver counters (ifconfig/sysmon). Drivers must increment these;
    // consumers must not fake them.
    uint64_t rx_packets, tx_packets;
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_errors, tx_errors;
    uint64_t rx_dropped, tx_dropped;
    uint64_t collisions;

    // Linked list for multiple network devices
    struct net_dev* next;
} net_dev_t;

// Global list of network devices
extern net_dev_t* network_devices;

void net_init();
void net_register_device(net_dev_t* net_dev);
void net_poll(); // RX drain + ARP/DHCP retransmission ticks
void net_register_loopback(); // lo device: loop frames back to ip_handle_packet
net_dev_t* net_get_loopback(); // NULL until net_register_loopback

#endif // NET_H
