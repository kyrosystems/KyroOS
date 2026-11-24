#include "virtio.h"
#include "port_io.h"
#include "log.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "kstring.h"
#include "net.h"
#include "arp.h"
#include "ip.h"

typedef struct {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed)) virtio_net_hdr_t;

typedef struct {
    net_dev_t net_dev;
    uint16_t io_base;
    uint16_t rx_qsize;
    uint16_t tx_qsize;
    vring_desc_t *rx_desc;
    vring_avail_t *rx_avail;
    vring_used_t *rx_used;
    vring_desc_t *tx_desc;
    vring_avail_t *tx_avail;
    vring_used_t *tx_used;
    uint8_t *rx_buffers;
    uint16_t rx_last_used_idx;
} virtio_net_t;

static virtio_net_t g_virtio_net;

static void virtio_net_send_packet(net_dev_t *dev, const uint8_t *data, size_t len) {
    virtio_net_t *vn = (virtio_net_t *)dev;
    if (!vn || !vn->io_base || !vn->tx_desc || !vn->tx_avail || vn->tx_qsize == 0) return;

    static virtio_net_hdr_t tx_hdr __attribute__((aligned(16)));
    static uint8_t tx_pkt_buf[2048] __attribute__((aligned(16)));
    memset(&tx_hdr, 0, sizeof(tx_hdr));

    if (len > sizeof(tx_pkt_buf)) len = sizeof(tx_pkt_buf);
    memcpy(tx_pkt_buf, data, len);

    // Desc 0: Header
    vn->tx_desc[0].addr = (uint64_t)vmm_virt_to_phys(&tx_hdr);
    vn->tx_desc[0].len = sizeof(virtio_net_hdr_t);
    vn->tx_desc[0].flags = VRING_DESC_F_NEXT;
    vn->tx_desc[0].next = 1;

    // Desc 1: Packet data
    vn->tx_desc[1].addr = (uint64_t)vmm_virt_to_phys(tx_pkt_buf);
    vn->tx_desc[1].len = (uint32_t)len;
    vn->tx_desc[1].flags = 0;
    vn->tx_desc[1].next = 0;

    uint16_t idx = vn->tx_avail->idx;
    vn->tx_avail->ring[idx % vn->tx_qsize] = 0;
    __asm__ volatile("" ::: "memory");
    vn->tx_avail->idx = idx + 1;

    // Notify queue 1 (TX)
    outw(vn->io_base + VIRTIO_REG_QUEUE_NOTIFY, 1);

    dev->tx_packets++;
    dev->tx_bytes += len;
}

static void virtio_net_poll(net_dev_t *dev, uint8_t *buffer, size_t size) {
    (void)buffer; (void)size;
    virtio_net_t *vn = (virtio_net_t *)dev;
    if (!vn || !vn->io_base || !vn->rx_used || vn->rx_qsize == 0) return;

    while (vn->rx_last_used_idx != vn->rx_used->idx) {
        uint16_t cur_used = vn->rx_last_used_idx % vn->rx_qsize;
        uint32_t id = vn->rx_used->ring[cur_used].id;
        uint32_t total_len = vn->rx_used->ring[cur_used].len;

        if (total_len > sizeof(virtio_net_hdr_t)) {
            uint32_t pkt_len = total_len - sizeof(virtio_net_hdr_t);
            uint8_t *pkt_data = vn->rx_buffers + (id * 2048) + sizeof(virtio_net_hdr_t);

            dev->rx_packets++;
            dev->rx_bytes += pkt_len;
            if (pkt_len >= sizeof(ethernet_header_t)) {
                ethernet_header_t *eth = (ethernet_header_t *)pkt_data;
                uint16_t type = __builtin_bswap16(eth->ether_type);
                if (type == 0x0806) {
                    arp_handle_packet(pkt_data + sizeof(ethernet_header_t), pkt_len - sizeof(ethernet_header_t));
                } else if (type == 0x0800) {
                    ip_handle_packet(dev, pkt_data + sizeof(ethernet_header_t), pkt_len - sizeof(ethernet_header_t));
                }
            }
        }

        // Put buffer back into avail ring
        uint16_t avail_idx = vn->rx_avail->idx;
        vn->rx_avail->ring[avail_idx % vn->rx_qsize] = id;
        __asm__ volatile("" ::: "memory");
        vn->rx_avail->idx = avail_idx + 1;

        vn->rx_last_used_idx++;
        outw(vn->io_base + VIRTIO_REG_QUEUE_NOTIFY, 0);
    }
}

void virtio_net_init(pci_device_t *dev) {
    klog(LOG_INFO, "virtio-net: Initializing VirtIO Network Device...");

    memset(&g_virtio_net, 0, sizeof(virtio_net_t));
    g_virtio_net.io_base = (uint16_t)dev->bars[0].base;
    if (!g_virtio_net.io_base) return;

    // Reset and acknowledge
    outb(g_virtio_net.io_base + VIRTIO_REG_DEVICE_STATUS, 0);
    outb(g_virtio_net.io_base + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // Read MAC address from offset 0x14
    for (int i = 0; i < 6; i++) {
        g_virtio_net.net_dev.mac_addr[i] = inb(g_virtio_net.io_base + 0x14 + i);
    }

    // Configure RX Queue (Queue 0)
    outw(g_virtio_net.io_base + VIRTIO_REG_QUEUE_SELECT, 0);
    g_virtio_net.rx_qsize = inw(g_virtio_net.io_base + VIRTIO_REG_QUEUE_SIZE);

    void *rx_q_phys = pmm_alloc_pages(2);
    void *rx_q_virt = vmm_phys_to_virt(rx_q_phys);
    memset(rx_q_virt, 0, PAGE_SIZE * 2);

    g_virtio_net.rx_desc = (vring_desc_t *)rx_q_virt;
    g_virtio_net.rx_avail = (vring_avail_t *)((uint8_t *)rx_q_virt + g_virtio_net.rx_qsize * sizeof(vring_desc_t));
    uint64_t rx_used_off = ((uint64_t)g_virtio_net.rx_avail + sizeof(vring_avail_t) + g_virtio_net.rx_qsize * sizeof(uint16_t) + 4095) & ~4095ULL;
    g_virtio_net.rx_used = (vring_used_t *)rx_used_off;

    outl(g_virtio_net.io_base + VIRTIO_REG_QUEUE_ADDRESS, (uint32_t)((uint64_t)rx_q_phys >> 12));

    // Allocate RX packet buffers
    void *rx_bufs_phys = pmm_alloc_pages((g_virtio_net.rx_qsize * 2048 + PAGE_SIZE - 1) / PAGE_SIZE);
    g_virtio_net.rx_buffers = (uint8_t *)vmm_phys_to_virt(rx_bufs_phys);
    memset(g_virtio_net.rx_buffers, 0, g_virtio_net.rx_qsize * 2048);

    for (uint16_t i = 0; i < g_virtio_net.rx_qsize; i++) {
        g_virtio_net.rx_desc[i].addr = (uint64_t)vmm_virt_to_phys(g_virtio_net.rx_buffers + (i * 2048));
        g_virtio_net.rx_desc[i].len = 2048;
        g_virtio_net.rx_desc[i].flags = VRING_DESC_F_WRITE;
        g_virtio_net.rx_desc[i].next = 0;
        g_virtio_net.rx_avail->ring[i] = i;
    }
    g_virtio_net.rx_avail->idx = g_virtio_net.rx_qsize;
    g_virtio_net.rx_last_used_idx = 0;

    // Configure TX Queue (Queue 1)
    outw(g_virtio_net.io_base + VIRTIO_REG_QUEUE_SELECT, 1);
    g_virtio_net.tx_qsize = inw(g_virtio_net.io_base + VIRTIO_REG_QUEUE_SIZE);

    void *tx_q_phys = pmm_alloc_pages(2);
    void *tx_q_virt = vmm_phys_to_virt(tx_q_phys);
    memset(tx_q_virt, 0, PAGE_SIZE * 2);

    g_virtio_net.tx_desc = (vring_desc_t *)tx_q_virt;
    g_virtio_net.tx_avail = (vring_avail_t *)((uint8_t *)tx_q_virt + g_virtio_net.tx_qsize * sizeof(vring_desc_t));
    uint64_t tx_used_off = ((uint64_t)g_virtio_net.tx_avail + sizeof(vring_avail_t) + g_virtio_net.tx_qsize * sizeof(uint16_t) + 4095) & ~4095ULL;
    g_virtio_net.tx_used = (vring_used_t *)tx_used_off;

    outl(g_virtio_net.io_base + VIRTIO_REG_QUEUE_ADDRESS, (uint32_t)((uint64_t)tx_q_phys >> 12));

    // Driver ready
    outb(g_virtio_net.io_base + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    // Register net_dev
    g_virtio_net.net_dev.send_packet = virtio_net_send_packet;
    g_virtio_net.net_dev.receive_packet = virtio_net_poll;
    g_virtio_net.net_dev.mtu = 1500;
    g_virtio_net.net_dev.flags = IFF_UP | IFF_RUNNING;
    net_register_device(&g_virtio_net.net_dev);

    klog(LOG_INFO, "virtio-net: Link Up. MAC: %02x:%02x:%02x:%02x:%02x:%02x",
         g_virtio_net.net_dev.mac_addr[0], g_virtio_net.net_dev.mac_addr[1],
         g_virtio_net.net_dev.mac_addr[2], g_virtio_net.net_dev.mac_addr[3],
         g_virtio_net.net_dev.mac_addr[4], g_virtio_net.net_dev.mac_addr[5]);
}
