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

#define E1000_REG_CTRL  0x00000
#define E1000_REG_STATUS 0x00008
#define E1000_REG_IMS   0x000D0
#define E1000_REG_RCTL  0x00100
#define E1000_REG_TCTL  0x00400
#define E1000_REG_RDBAL 0x02800
#define E1000_REG_RDBAH 0x02804
#define E1000_REG_RDLEN 0x02808
#define E1000_REG_RDH   0x02810
#define E1000_REG_RDT   0x02818
#define E1000_REG_TDBAL 0x03800
#define E1000_REG_TDLEN 0x03808
#define E1000_REG_TDH   0x03810
#define E1000_REG_TDT   0x03818
#define E1000_REG_RA    0x05400

#define RX_NUM_DESC 32
#define TX_NUM_DESC 8

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
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

static void e1000_send_packet(net_dev_t *dev, const uint8_t *data, size_t len) {
    e1000_t *d = (e1000_t *)dev;
    uint32_t i = d->tx_ptr;
    
    memcpy(d->tx_buffers + (i * 2048), data, len);
    d->tx_ring[i].length = len;
    d->tx_ring[i].cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP, IFCS, RS
    d->tx_ring[i].status = 0;
    
    d->tx_ptr = (i + 1) % TX_NUM_DESC;
    d->mmio[E1000_REG_TDT / 4] = d->tx_ptr;
    
    while (!(d->tx_ring[i].status & 0x01)) __asm__("pause");
}

void e1000_poll(net_dev_t *dev, uint8_t *buffer, size_t size) {
    (void)buffer; (void)size;
    e1000_t *d = (e1000_t *)dev;
    while (d->rx_ring[d->rx_ptr].status & 0x01) { // DD bit
        uint8_t *buf = d->rx_buffers + (d->rx_ptr * 2048);
        uint16_t len = d->rx_ring[d->rx_ptr].length;

        ethernet_header_t *eth = (ethernet_header_t *)buf;
        uint16_t type = __builtin_bswap16(eth->ether_type);

        if (type == 0x0806) { // ARP
            arp_handle_packet(buf + sizeof(ethernet_header_t), len - sizeof(ethernet_header_t));
        } else if (type == 0x0800) { // IPv4
            ip_handle_packet(dev, buf + sizeof(ethernet_header_t), len - sizeof(ethernet_header_t));
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
    
    // Enable Bus Mastering
    uint16_t pci_cmd = pci_config_read_word(pdev->bus, pdev->device, pdev->func, 0x04);
    pci_config_write_word(pdev->bus, pdev->device, pdev->func, 0x04, pci_cmd | 0x04);

    // TX Setup
    d->tx_ring = (struct e1000_tx_desc *)kmalloc(TX_NUM_DESC * sizeof(struct e1000_tx_desc));
    d->tx_buffers = (uint8_t *)kmalloc(TX_NUM_DESC * 2048);
    for (int i = 0; i < TX_NUM_DESC; i++) {
        d->tx_ring[i].addr = (uint64_t)d->tx_buffers + (i * 2048) - kernel_hhdm_offset;
        d->tx_ring[i].status = 0x01;
    }
    d->mmio[E1000_REG_TDBAL / 4] = (uint32_t)((uint64_t)d->tx_ring - kernel_hhdm_offset);
    d->mmio[(E1000_REG_TDBAL + 4) / 4] = 0;
    d->mmio[E1000_REG_TDLEN / 4] = TX_NUM_DESC * sizeof(struct e1000_tx_desc);
    d->mmio[E1000_REG_TDH / 4] = 0;
    d->mmio[E1000_REG_TDT / 4] = 0;
    d->mmio[E1000_REG_TCTL / 4] = (1 << 1) | (1 << 3) | (0xF << 4) | (0x40 << 12); // EN, PSP, CT, COLD

    // RX Setup
    d->rx_ring = (struct e1000_rx_desc *)kmalloc(RX_NUM_DESC * sizeof(struct e1000_rx_desc));
    d->rx_buffers = (uint8_t *)kmalloc(RX_NUM_DESC * 2048);
    for (int i = 0; i < RX_NUM_DESC; i++) {
        d->rx_ring[i].addr = (uint64_t)d->rx_buffers + (i * 2048) - kernel_hhdm_offset;
        d->rx_ring[i].status = 0;
    }
    d->mmio[E1000_REG_RDBAL / 4] = (uint32_t)((uint64_t)d->rx_ring - kernel_hhdm_offset);
    d->mmio[E1000_REG_RDBAH / 4] = 0;
    d->mmio[E1000_REG_RDLEN / 4] = RX_NUM_DESC * sizeof(struct e1000_rx_desc);
    d->mmio[E1000_REG_RDH / 4] = 0;
    d->mmio[E1000_REG_RDT / 4] = RX_NUM_DESC - 1;
    d->mmio[E1000_REG_RCTL / 4] = (1 << 1) | (1 << 2) | (1 << 4) | (1 << 15) | (0 << 16); // EN, SBP, BAM, BSIZE=2048

    // MAC Address
    uint32_t low = d->mmio[E1000_REG_RA / 4];
    uint32_t high = d->mmio[(E1000_REG_RA + 4) / 4];
    d->net_dev.mac_addr[0] = low & 0xFF; d->net_dev.mac_addr[1] = (low >> 8) & 0xFF;
    d->net_dev.mac_addr[2] = (low >> 16) & 0xFF; d->net_dev.mac_addr[3] = (low >> 24) & 0xFF;
    d->net_dev.mac_addr[4] = high & 0xFF; d->net_dev.mac_addr[5] = (high >> 8) & 0xFF;

    d->net_dev.send_packet = e1000_send_packet;
    d->net_dev.receive_packet = (net_receive_packet_t)e1000_poll;
    net_register_device(&d->net_dev);
    
    klog(LOG_INFO, "E1000: Link Up. MAC: %02x:%02x:%02x:%02x:%02x:%02x", 
         d->net_dev.mac_addr[0], d->net_dev.mac_addr[1], d->net_dev.mac_addr[2],
         d->net_dev.mac_addr[3], d->net_dev.mac_addr[4], d->net_dev.mac_addr[5]);
}
