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

// Network device structure
typedef struct net_dev {
    device_t* dev; // Pointer to the underlying generic device
    uint8_t mac_addr[6];
    net_send_packet_t send_packet;
    net_receive_packet_t receive_packet;
    // For zero-copy: a ring buffer or similar mechanism for received packets

    
    // Linked list for multiple network devices
    struct net_dev* next;
} net_dev_t;

// Global list of network devices
extern net_dev_t* network_devices;

void net_init();
void net_register_device(net_dev_t* net_dev);

#endif // NET_H
