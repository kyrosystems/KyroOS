#include "net.h"
#include "heap.h"
#include "log.h"
#include "kstring.h"

net_dev_t *network_devices = NULL;

void net_init() {
    klog(LOG_INFO, "Network: Core initialized.");
}

void net_register_device(net_dev_t *dev) {
    if (!dev) return;
    dev->next = network_devices;
    network_devices = dev;
}

void net_send_packet(uint8_t *data, size_t len) {
    if (network_devices && network_devices->send_packet) {
        network_devices->send_packet(network_devices, data, len);
    }
}
