#include "net.h"
#include "arp.h"
#include "dhcp.h"
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

/* One network service tick: drain NIC RX rings + drive ARP/DHCP
 * retransmissions. Call from the main shell loop and blocking socket
 * waits so the stack self-heals after packet loss. */
void net_poll(void) {
    net_dev_t *dev = network_devices;
    while (dev) {
        if (dev->receive_packet) dev->receive_packet(dev, NULL, 0);
        dev = dev->next;
    }
    arp_poll();
    dhcp_poll();
}

void net_send_packet(uint8_t *data, size_t len) {
    if (network_devices && network_devices->send_packet) {
        network_devices->tx_packets++;
        network_devices->tx_bytes += len;
        network_devices->send_packet(network_devices, data, len);
    }
}

// ---- Loopback interface (lo) ----
// Real loopback device: 'sends' by feeding the frame back into the IP
// receive path, with genuine rx/tx counters. Enables deterministic
// self-tests (ping 127.0.0.1) without external network.
#include "ip.h"
#include "arp.h"

static net_dev_t *lo_dev = NULL;
static uint8_t  lo_rxbuf[2048];

static void lo_send_packet(net_dev_t *dev, const uint8_t *buffer, size_t size) {
    dev->tx_packets++;
    dev->tx_bytes += size;
    /* Loop the Ethernet frame back into the IP receive path immediately.
       Only IPv4 frames are meaningful here; anything else is counted and dropped. */
    if (size >= sizeof(ethernet_header_t)) {
        ethernet_header_t *eth = (ethernet_header_t *)buffer;
        uint16_t type = __builtin_bswap16(eth->ether_type);
        if (type == 0x0800 && size - sizeof(ethernet_header_t) <= sizeof(lo_rxbuf)) {
            memcpy(lo_rxbuf, buffer + sizeof(ethernet_header_t),
                   size - sizeof(ethernet_header_t));
            dev->rx_packets++;
            dev->rx_bytes += size;
            ip_handle_packet(dev, lo_rxbuf, size - sizeof(ethernet_header_t));
        } else {
            dev->rx_dropped++;
        }
    } else {
        dev->rx_dropped++;
    }
}

static void lo_poll(net_dev_t *dev, uint8_t *buffer, size_t size) {
    (void)dev; (void)buffer; (void)size; /* nothing async on loopback */
}

void net_register_loopback() {
    if (lo_dev) return;
    lo_dev = (net_dev_t *)kmalloc(sizeof(net_dev_t));
    if (!lo_dev) return;
    memset(lo_dev, 0, sizeof(net_dev_t));
    for (int i = 0; i < 6; i++) lo_dev->mac_addr[i] = 0;
    lo_dev->send_packet    = lo_send_packet;
    lo_dev->receive_packet = lo_poll;
    lo_dev->ip_addr  = 0x7F000001;  /* 127.0.0.1 host-readable */
    lo_dev->netmask  = 0xFF000000;  /* 255.0.0.0 */
    lo_dev->gateway  = 0;
    lo_dev->mtu      = 1500;
    lo_dev->flags    = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
    /* Append at the END of the device list: DHCP/ARP/UDP use the head
       (first physical NIC), and lo must not shadow it. */
    if (!network_devices) {
        net_register_device(lo_dev);
    } else {
        net_dev_t *d = network_devices;
        while (d->next) d = d->next;
        d->next = lo_dev;
        lo_dev->next = NULL;
    }
    klog(LOG_INFO, "Network: loopback (lo) registered, 127.0.0.1");
}net_dev_t* net_get_loopback() {
    return lo_dev;
}

