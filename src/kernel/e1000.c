#include "e1000.h"
#include "arp.h"
#include "dhcp.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "net.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "ip.h"

#define E1000_REG_CTRL     0x00000
#define E1000_REG_STATUS   0x00008
#define E1000_REG_EERD     0x00014
#define E1000_REG_IMS      0x000D0
#define E1000_REG_RCTL     0x00100
#define E1000_REG_TCTL     0x00400
#define E1000_REG_RDBAL    0x02800
#define E1000_REG_RDBAH    0x02804
#define E1000_REG_RDLEN    0x02808
#define E1000_REG_RDH      0x02810
#define E1000_REG_RDT      0x02818
#define E1000_REG_TDBAL    0x03800
#define E1000_REG_TDBAL_HI 0x03804
#define E1000_REG_TDLEN    0x03808
#define E1000_REG_TDH      0x03810
#define E1000_REG_TDT      0x03818
#define E1000_REG_RA       0x05400
#define E1000_REG_MTA      0x05200

#define RX_NUM_DESC 64
#define TX_NUM_DESC 32
#define E1000_RX_BUF_SIZE 2048
#define E1000_TX_BUF_SIZE 2048

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

typedef struct {
    net_dev_t net_dev;
    volatile uint32_t *mmio;
    struct e1000_rx_desc *rx_ring;
    uint8_t *rx_buffers;
    uint32_t rx_ptr;
    struct e1000_tx_desc *tx_ring;
    uint8_t *tx_buffers;
    uint32_t tx_ptr;
} e1000_t;

static void e1000_write_mac(e1000_t *d, const uint8_t mac[6]) {
    uint32_t low = ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
                   ((uint32_t)mac[1] << 8)  | (uint32_t)mac[0];
    uint32_t high = ((uint32_t)mac[5] << 8) | (uint32_t)mac[4] | (1U << 31);
    d->mmio[E1000_REG_RA / 4] = low;
    d->mmio[(E1000_REG_RA + 4) / 4] = high;
}

static void e1000_read_mac(e1000_t *d, uint8_t mac[6]) {
    uint32_t low = d->mmio[E1000_REG_RA / 4];
    uint32_t high = d->mmio[(E1000_REG_RA + 4) / 4];
    mac[0] = low & 0xFF;
    mac[1] = (low >> 8) & 0xFF;
    mac[2] = (low >> 16) & 0xFF;
    mac[3] = (low >> 24) & 0xFF;
    mac[4] = high & 0xFF;
    mac[5] = (high >> 8) & 0xFF;
}

static uint16_t e1000_read_eeprom(e1000_t *d, uint8_t addr) {
    uint32_t val = 0;
    d->mmio[E1000_REG_EERD / 4] = ((uint32_t)addr << 8) | 1;
    for (int i = 0; i < 20000; i++) {
        val = d->mmio[E1000_REG_EERD / 4];
        if (val & (1 << 4)) return (uint16_t)(val >> 16);
        if (val & (1 << 1)) return (uint16_t)(val >> 16);
    }
    d->mmio[E1000_REG_EERD / 4] = ((uint32_t)addr << 2) | 1;
    for (int i = 0; i < 20000; i++) {
        val = d->mmio[E1000_REG_EERD / 4];
        if (val & (1 << 4)) return (uint16_t)(val >> 16);
    }
    return 0;
}

static void e1000_send_packet(net_dev_t *dev, const uint8_t *data, size_t len) {
    e1000_t *d = (e1000_t *)dev;
    if (len > E1000_TX_BUF_SIZE) { dev->tx_errors++; len = E1000_TX_BUF_SIZE; }

    uint32_t i = d->tx_ptr;
    int wait_limit = 10000;
    while ((d->tx_ring[i].cmd != 0 && !(d->tx_ring[i].status & 0x01)) && --wait_limit > 0) {
        __asm__("pause");
    }

    dev->tx_packets++;
    dev->tx_bytes += len;

    memcpy(d->tx_buffers + (i * E1000_TX_BUF_SIZE), data, len);

    d->tx_ring[i].length = len;
    d->tx_ring[i].cso = 0;
    d->tx_ring[i].cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP | IFCS | RS
    d->tx_ring[i].status = 0;
    d->tx_ring[i].css = 0;
    d->tx_ring[i].special = 0;

    __asm__ __volatile__("" ::: "memory");

    d->tx_ptr = (i + 1) % TX_NUM_DESC;
    d->mmio[E1000_REG_TDT / 4] = d->tx_ptr;
}

void e1000_poll(net_dev_t *dev, uint8_t *buffer, size_t size) {
    (void)buffer; (void)size;
    e1000_t *d = (e1000_t *)dev;

    while (d->rx_ring[d->rx_ptr].status & 0x01) {
        uint8_t *buf = d->rx_buffers + (d->rx_ptr * E1000_RX_BUF_SIZE);
        uint16_t len = d->rx_ring[d->rx_ptr].length;

        if (len >= sizeof(ethernet_header_t)) {
            dev->rx_packets++;
            dev->rx_bytes += len;
            ethernet_header_t *eth = (ethernet_header_t *)buf;
            uint16_t type = __builtin_bswap16(eth->ether_type);

            if (type == 0x0806) {
                arp_handle_packet(buf + sizeof(ethernet_header_t), len - sizeof(ethernet_header_t));
            } else if (type == 0x0800) {
                ip_handle_packet(dev, buf + sizeof(ethernet_header_t), len - sizeof(ethernet_header_t));
            }
        } else {
            dev->rx_dropped++;
        }

        d->rx_ring[d->rx_ptr].status = 0;
        uint32_t old_ptr = d->rx_ptr;
        d->rx_ptr = (d->rx_ptr + 1) % RX_NUM_DESC;
        d->mmio[E1000_REG_RDT / 4] = old_ptr;
    }
}

void e1000_driver_init(pci_device_t *pdev) {
    klog(LOG_INFO, "E1000: Initializing Driver...");

    e1000_t *d = kmalloc(sizeof(e1000_t));
    memset(d, 0, sizeof(e1000_t));

    extern uint64_t kernel_hhdm_offset;
    d->mmio = (uint32_t *)(pdev->bars[0].base + kernel_hhdm_offset);

    uint16_t pci_cmd = pci_config_read_word(pdev->bus, pdev->device, pdev->func, 0x04);
    pci_config_write_word(pdev->bus, pdev->device, pdev->func, 0x04, pci_cmd | 0x07);

    // Assert SLU (Set Link Up) and ASDE (Auto-Speed Detection) in Device Control
    d->mmio[E1000_REG_CTRL / 4] |= (1 << 6) | (1 << 5);

    d->tx_ring = kmalloc(TX_NUM_DESC * sizeof(struct e1000_tx_desc));
    d->tx_buffers = kmalloc(TX_NUM_DESC * E1000_TX_BUF_SIZE);
    memset(d->tx_ring, 0, TX_NUM_DESC * sizeof(struct e1000_tx_desc));
    for (int i = 0; i < TX_NUM_DESC; i++) {
        d->tx_ring[i].addr = (uint64_t)d->tx_buffers + (i * E1000_TX_BUF_SIZE) - kernel_hhdm_offset;
    }
    d->mmio[E1000_REG_TDBAL / 4] = (uint32_t)((uint64_t)d->tx_ring - kernel_hhdm_offset);
    d->mmio[E1000_REG_TDBAL_HI / 4] = 0;
    d->mmio[E1000_REG_TDLEN / 4] = TX_NUM_DESC * sizeof(struct e1000_tx_desc);
    d->mmio[E1000_REG_TDH / 4] = 0;
    d->mmio[E1000_REG_TDT / 4] = 0;
    d->mmio[E1000_REG_TCTL / 4] = (1 << 1) | (1 << 3) | (0xF << 4) | (0x40 << 12);

    d->rx_ring = kmalloc(RX_NUM_DESC * sizeof(struct e1000_rx_desc));
    d->rx_buffers = kmalloc(RX_NUM_DESC * E1000_RX_BUF_SIZE);
    memset(d->rx_ring, 0, RX_NUM_DESC * sizeof(struct e1000_rx_desc));
    for (int i = 0; i < RX_NUM_DESC; i++) {
        d->rx_ring[i].addr = (uint64_t)d->rx_buffers + (i * E1000_RX_BUF_SIZE) - kernel_hhdm_offset;
    }
    d->mmio[E1000_REG_RDBAL / 4] = (uint32_t)((uint64_t)d->rx_ring - kernel_hhdm_offset);
    d->mmio[E1000_REG_RDBAH / 4] = 0;
    d->mmio[E1000_REG_RDLEN / 4] = RX_NUM_DESC * sizeof(struct e1000_rx_desc);
    d->mmio[E1000_REG_RDH / 4] = 0;
    d->mmio[E1000_REG_RDT / 4] = RX_NUM_DESC - 1;
    // Receiver enable + Store bad packets + Unicast Promiscuous + Multicast Promiscuous + Broadcast accept
    d->mmio[E1000_REG_RCTL / 4] = (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 15);

    e1000_read_mac(d, d->net_dev.mac_addr);
    bool invalid_mac = true;
    for (int i = 0; i < 6; i++) {
        if (d->net_dev.mac_addr[i] != 0x00 && d->net_dev.mac_addr[i] != 0xFF) {
            invalid_mac = false;
            break;
        }
    }
    if (invalid_mac) {
        uint16_t w0 = e1000_read_eeprom(d, 0);
        uint16_t w1 = e1000_read_eeprom(d, 1);
        uint16_t w2 = e1000_read_eeprom(d, 2);
        if ((w0 || w1 || w2) && (w0 != 0xFFFF)) {
            d->net_dev.mac_addr[0] = w0 & 0xFF;
            d->net_dev.mac_addr[1] = (w0 >> 8) & 0xFF;
            d->net_dev.mac_addr[2] = w1 & 0xFF;
            d->net_dev.mac_addr[3] = (w1 >> 8) & 0xFF;
            d->net_dev.mac_addr[4] = w2 & 0xFF;
            d->net_dev.mac_addr[5] = (w2 >> 8) & 0xFF;
            e1000_write_mac(d, d->net_dev.mac_addr);
            invalid_mac = false;
        }
    }
    if (invalid_mac) {
        static const uint8_t default_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
        memcpy(d->net_dev.mac_addr, default_mac, 6);
        e1000_write_mac(d, default_mac);
    }

    for (int i = 0; i < 128; i++) d->mmio[(E1000_REG_MTA / 4) + i] = 0;

    d->net_dev.send_packet = e1000_send_packet;
    d->net_dev.receive_packet = (net_receive_packet_t)e1000_poll;
    d->net_dev.mtu = 1500;
    d->net_dev.flags = IFF_UP | IFF_RUNNING;
    d->net_dev.ip_addr = 0;   // configured by DHCP
    d->net_dev.netmask = 0;
    d->net_dev.gateway = 0;
    d->net_dev.rx_packets = d->net_dev.tx_packets = 0;
    d->net_dev.rx_bytes = d->net_dev.tx_bytes = 0;
    d->net_dev.rx_errors = d->net_dev.tx_errors = 0;
    d->net_dev.rx_dropped = d->net_dev.tx_dropped = 0;
    d->net_dev.collisions = 0;
    net_register_device(&d->net_dev);

    klog(LOG_INFO, "E1000: Link Up. MAC: %02x:%02x:%02x:%02x:%02x:%02x",
         d->net_dev.mac_addr[0], d->net_dev.mac_addr[1], d->net_dev.mac_addr[2],
         d->net_dev.mac_addr[3], d->net_dev.mac_addr[4], d->net_dev.mac_addr[5]);
}
