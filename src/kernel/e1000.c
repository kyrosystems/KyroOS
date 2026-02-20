#include "e1000.h"
#include "arp.h" // Include ARP header
#include "dhcp.h"
#include "deviceman.h"
#include "heap.h"
#include "ip.h" // Include IP header
#include "isr.h"
#include "kstring.h" // For memset, memcpy
#include "log.h"
#include "net.h"
#include "pci.h"
#include "pmm.h"
#include "port_io.h"
#include "vmm.h"
#include "scheduler.h" // For schedule()

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID_82540EM 0x100e
#define E1000_DEVICE_ID_82545EM 0x100f
#define E1000_DEVICE_ID_82543GC 0x107b

// Memory-mapped registers offsets
#define E1000_REG_CTRL 0x00000
#define E1000_REG_STATUS 0x00008
#define E1000_REG_EEPROM_RD 0x00014
#define E1000_REG_IMC 0x00028 // Interrupt Mask Clear
#define E1000_REG_IMS 0x000D0 // Interrupt Mask Set
#define E1000_REG_ICR 0x000C0 // Interrupt Cause Read
#define E1000_REG_RCTL 0x00100
#define E1000_REG_TCTL 0x00400
#define E1000_REG_RDBAL 0x02800
#define E1000_REG_RDLEN 0x02808
#define E1000_REG_RDH 0x02810
#define E1000_REG_RDT 0x02818
#define E1000_REG_TDBAL 0x03800
#define E1000_REG_TDLEN 0x03808
#define E1000_REG_TDH 0x03810
#define E1000_REG_TDT 0x03818
#define E1000_REG_MTA 0x05200 // Multicast Table Array
#define E1000_REG_RA 0x05400  // Receive Address Register (MAC addr)

// RCTL bits
#define E1000_RCTL_EN (1 << 1)     // Receiver Enable
#define E1000_RCTL_SBP (1 << 2)    // Store Bad Packets
#define E1000_RCTL_UPE (1 << 3)    // Unicast Promiscuous Enable
#define E1000_RCTL_MPE (1 << 4)    // Multicast Promiscuous Enable
#define E1000_RCTL_LPE (1 << 5)    // Long Packet Enable
#define E1000_RCTL_LBM_NO (0 << 6) // Loopback Mode - No Loopback
#define E1000_RCTL_BAM (1 << 15)   // Broadcast Accept Mode
#define E1000_RCTL_VFE (1 << 18)   // VLAN Filter Enable
#define E1000_RCTL_CFIEN (1 << 19) // Canonical Form Indicator Enable
#define E1000_RCTL_CFI (1 << 20)   // Canonical Form Indicator
#define E1000_RCTL_DPF (1 << 22)   // Discard Pause Frames
#define E1000_RCTL_PMCF (1 << 23)  // Pass MAC Control Frames
#define E1000_RCTL_SECRC (1 << 26) // Strip Ethernet CRC

// RCTL BSIZE bits (Packet Buffer Size)
#define E1000_RCTL_BSIZE_2048 (0 << 12)

// TCTL bits
#define E1000_TCTL_EN (1 << 1)       // Transmit Enable
#define E1000_TCTL_PSP (1 << 3)      // Pad Short Packets
#define E1000_TCTL_CT (0x0F << 4)    // Collision Threshold (10h)
#define E1000_TCTL_COLD (0x3F << 12) // Collision Distance (40h)

// Transmit Descriptor Command (CMD) bits
#define E1000_TXD_CMD_EOP (1 << 0) // End Of Packet
#define E1000_TXD_CMD_IC (1 << 1)  // Insert Checksum
#define E1000_TXD_CMD_RS (1 << 3)  // Report Status

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

// Receive Descriptor
struct e1000_rx_desc {
  uint64_t addr;   // Address of data buffer
  uint16_t length; // Length of packet
  uint16_t checksum;
  uint8_t status; // Status
  uint8_t errors; // Errors
  uint16_t special;
} __attribute__((packed));

// Transmit Descriptor
struct e1000_tx_desc {
  uint64_t addr;   // Address of data buffer
  uint16_t length; // Length of packet
  uint8_t cso;     // Checksum Offset
  uint8_t cmd;     // Command
  uint8_t status;  // Status
  uint8_t css;
  uint16_t special;
} __attribute__((packed));

// E1000 device specific structure
typedef struct e1000_device {
  net_dev_t net_dev;
  volatile uint32_t *mmio_base; // Base address of MMIO registers
  uint16_t pci_command_reg;     // Original value of PCI Command register
  uint32_t irq;                 // IRQ number

  struct e1000_rx_desc *rx_descs;
  uint64_t rx_descs_phys;
  uint8_t *rx_buffers[E1000_NUM_RX_DESC];
  volatile uint16_t rx_cur; // Next descriptor to give to the hardware

  struct e1000_tx_desc *tx_descs;
  uint64_t tx_descs_phys;
  uint8_t *tx_buffers[E1000_NUM_TX_DESC];
  volatile uint16_t tx_cur; // Next descriptor to send
} e1000_device_t;

// --- MMIO Register Access ---
static inline uint32_t e1000_read_reg(e1000_device_t *dev, uint32_t offset) {
  return dev->mmio_base[offset / 4];
}

static inline void e1000_write_reg(e1000_device_t *dev, uint32_t offset,
                                   uint32_t val) {
  dev->mmio_base[offset / 4] = val;
}

// --- Network Core Implementation (from net.h) ---
net_dev_t *network_devices = NULL;

void net_init() {
  network_devices = NULL;
  klog(LOG_INFO, "Network core initialized.");
}

void net_register_device(net_dev_t *net_dev) {
  if (!net_dev)
    return;

  net_dev->next = network_devices;
  network_devices = net_dev;
  klog(LOG_INFO, "Network device registered.");
}

// --- E1000 Interrupt Handler ---
static void e1000_interrupt_handler(struct registers *regs) {
  (void)regs; // Suppress unused parameter warning

  e1000_device_t *e1000_dev =
      (e1000_device_t *)((device_t *)network_devices->dev)->private_data;

  uint32_t icr = e1000_read_reg(e1000_dev, E1000_REG_ICR);
  if (!icr)
    return; // Not our interrupt

  /* klog_print_str("E1000 Interrupt! ICR: ");
  klog_print_hex(icr);
  klog_putchar('\n'); */

  // Handle received packets
  while ((e1000_dev->rx_descs[e1000_dev->rx_cur].status & 0x01)) { // DD bit set
    uint16_t length = e1000_dev->rx_descs[e1000_dev->rx_cur].length;
    uint8_t *packet_buffer = e1000_dev->rx_buffers[e1000_dev->rx_cur];

    if (length > sizeof(ethernet_header_t)) {
      ethernet_header_t *eth_hdr = (ethernet_header_t *)packet_buffer;
      uint16_t ether_type = __builtin_bswap16(eth_hdr->ether_type);

      switch (ether_type) {
      case ARP_ETHER_TYPE:
        // klog(LOG_INFO, "E1000: Received ARP packet.");
        arp_handle_packet(packet_buffer + sizeof(ethernet_header_t),
                          length - sizeof(ethernet_header_t));
        break;
      case 0x0800: // ETHERTYPE_IPV4
        // klog(LOG_INFO, "E1000: Received IPv4 packet.");
        ip_handle_packet(network_devices,
                         packet_buffer + sizeof(ethernet_header_t),
                         length - sizeof(ethernet_header_t));
        break;
      default:
        klog(LOG_INFO, "E1000: Received unknown EtherType.");
        break;
      }
    } else {
      klog(LOG_WARN, "E1000: Received too small packet.");
    }

    // Clear status and give descriptor back to hardware
    e1000_dev->rx_descs[e1000_dev->rx_cur].status = 0;
    e1000_dev->rx_cur = (e1000_dev->rx_cur + 1) % E1000_NUM_RX_DESC;
    e1000_write_reg(e1000_dev, E1000_REG_RDT, e1000_dev->rx_cur - 1);
  }
}

// hello Linux Torvalds, i love fedora and debian(not really), arch is the best distro btw
// i use windows for gaming only, dont judge me
// i also use opensuse as my secondary os
// and i also use macOS tahoe just 4 fun

// --- E1000 Send Packet ---
static void e1000_send_packet(net_dev_t *net_dev, const uint8_t *buffer,
                              size_t size) {
  e1000_device_t *e1000_dev = (e1000_device_t *)net_dev->dev->private_data;

  // Ensure packet size is within limits
  if (size > 2048) {
    klog(LOG_WARN, "E1000: Packet too large to send.");
    return;
  }

  // Get the next transmit descriptor
  uint16_t current_tdt = e1000_read_reg(e1000_dev, E1000_REG_TDT);
  struct e1000_tx_desc *desc = &e1000_dev->tx_descs[current_tdt];

  // Wait for descriptor to be available
  while (!(desc->status & 0x01)) { // DD bit
    // Yield to other threads while waiting for the descriptor to become available
    schedule();
  }

  // Copy data to transmit buffer
  memcpy(e1000_dev->tx_buffers[current_tdt], buffer, size);

  // Fill descriptor
  desc->length = size;
  desc->cmd =
      E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS; // End of Packet, Report Status
  desc->status = 0;                         // Clear status

  // Advance TDT
  e1000_write_reg(e1000_dev, E1000_REG_TDT,
                  (current_tdt + 1) % E1000_NUM_TX_DESC);
  klog(LOG_INFO, "E1000: Sent a packet.");
}

// --- E1000 Driver Core Functions ---
static int e1000_probe(device_t *dev) {
  if (dev->vendor_id == E1000_VENDOR_ID &&
      (dev->device_id == E1000_DEVICE_ID_82540EM ||
       dev->device_id == E1000_DEVICE_ID_82545EM ||
       dev->device_id == E1000_DEVICE_ID_82543GC)) {
    klog(LOG_INFO, "E1000: Found compatible Intel Ethernet device.");
    return 1;
  }
  return 0;
}

static int e1000_attach(device_t *dev) {
  klog(LOG_INFO, "E1000: Attaching driver to device.");

  e1000_device_t *e1000_dev = (e1000_device_t *)kmalloc(sizeof(e1000_device_t));
  if (!e1000_dev) {
    klog(LOG_ERROR, "E1000: Failed to allocate e1000_device_t.");
    return -1;
  }
  memset(e1000_dev, 0, sizeof(e1000_device_t));
  e1000_dev->net_dev.dev = dev;
  dev->private_data = e1000_dev;

  // 1. Enable PCI Bus Mastering
  uint16_t pci_command_reg =
      pci_read_config_word(dev->bus, dev->device, dev->func, PCI_COMMAND);
  e1000_dev->pci_command_reg = pci_command_reg; // Save original
  pci_command_reg |= (1 << 2);                  // Set Bus Master Enable bit
  pci_write_config_word(dev->bus, dev->device, dev->func, PCI_COMMAND,
                        pci_command_reg);

  // 2. Map MMIO registers using HHDM offset
  uint32_t bar0 = pci_read_config_dword(dev->bus, dev->device, dev->func, 0x10);
  uint32_t mmio_phys_base = bar0 & 0xFFFFFFF0;
  e1000_dev->mmio_base =
      (volatile uint32_t *)(uint64_t)(mmio_phys_base + kernel_hhdm_offset);

  // 3. Read MAC Address
  uint32_t mac_low = e1000_read_reg(e1000_dev, E1000_REG_RA);
  uint32_t mac_high = e1000_read_reg(e1000_dev, E1000_REG_RA + 4);
  e1000_dev->net_dev.mac_addr[0] = (uint8_t)(mac_low & 0xFF);
  e1000_dev->net_dev.mac_addr[1] = (uint8_t)((mac_low >> 8) & 0xFF);
  e1000_dev->net_dev.mac_addr[2] = (uint8_t)((mac_low >> 16) & 0xFF);
  e1000_dev->net_dev.mac_addr[3] = (uint8_t)((mac_low >> 24) & 0xFF);
  e1000_dev->net_dev.mac_addr[4] = (uint8_t)(mac_high & 0xFF);
  e1000_dev->net_dev.mac_addr[5] = (uint8_t)((mac_high >> 8) & 0xFF);

  char mac_buf[64];
  ksprintf(mac_buf, "E1000: MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
           e1000_dev->net_dev.mac_addr[0], e1000_dev->net_dev.mac_addr[1],
           e1000_dev->net_dev.mac_addr[2], e1000_dev->net_dev.mac_addr[3],
           e1000_dev->net_dev.mac_addr[4], e1000_dev->net_dev.mac_addr[5]);
  klog(LOG_INFO, "%s", mac_buf);

  // 4. Disable interrupts
  e1000_write_reg(e1000_dev, E1000_REG_IMC, 0xFFFFFFFF);

  // 5. Reset the device
  e1000_write_reg(e1000_dev, E1000_REG_CTRL,
                  e1000_read_reg(e1000_dev, E1000_REG_CTRL) | (1 << 26));
  while (e1000_read_reg(e1000_dev, E1000_REG_CTRL) & (1 << 26))
    ;

  // 6. Setup Receive and Transmit Descriptor Lists
  // Allocate memory for Rx Descriptors
  void *rx_phys = pmm_alloc_page();
  if (!rx_phys) {
    klog(LOG_ERROR, "E1000: Failed to allocate Rx descriptor memory.");
    return -1;
  }
  e1000_dev->rx_descs_phys = (uint64_t)rx_phys;
  e1000_dev->rx_descs = (struct e1000_rx_desc *)p_to_v(rx_phys);
  memset(e1000_dev->rx_descs, 0,
         E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));

  for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
    void *buf_phys = pmm_alloc_page();
    if (!buf_phys) {
      klog(LOG_ERROR, "E1000: Failed to allocate Rx buffer.");
      return -1;
    }
    e1000_dev->rx_buffers[i] = (uint8_t *)p_to_v(buf_phys);
    e1000_dev->rx_descs[i].addr = (uint64_t)buf_phys;
    e1000_dev->rx_descs[i].status = 0;
  }
  e1000_dev->rx_cur = 0;

  // Allocate memory for Tx Descriptors
  void *tx_phys = pmm_alloc_page();
  if (!tx_phys) {
    klog(LOG_ERROR, "E1000: Failed to allocate Tx descriptor memory.");
    return -1;
  }
  e1000_dev->tx_descs_phys = (uint64_t)tx_phys;
  e1000_dev->tx_descs = (struct e1000_tx_desc *)p_to_v(tx_phys);
  memset(e1000_dev->tx_descs, 0,
         E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));

  for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
    void *buf_phys = pmm_alloc_page();
    if (!buf_phys) {
      klog(LOG_ERROR, "E1000: Failed to allocate Tx buffer.");
      return -1;
    }
    e1000_dev->tx_buffers[i] = (uint8_t *)p_to_v(buf_phys);
    e1000_dev->tx_descs[i].addr = (uint64_t)buf_phys;
    e1000_dev->tx_descs[i].cmd =
        E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS; // Default commands
    e1000_dev->tx_descs[i].status = 0x01;     // Set DD bit initially
  }
  e1000_dev->tx_cur = 0;

  // 7. Program the descriptor base addresses and lengths
  e1000_write_reg(e1000_dev, E1000_REG_RDBAL,
                  (uint32_t)e1000_dev->rx_descs_phys);
  e1000_write_reg(e1000_dev, E1000_REG_RDBAL + 4,
                  (uint32_t)(e1000_dev->rx_descs_phys >> 32));
  e1000_write_reg(e1000_dev, E1000_REG_RDLEN,
                  E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
  e1000_write_reg(e1000_dev, E1000_REG_RDH, 0);
  e1000_write_reg(e1000_dev, E1000_REG_RDT, E1000_NUM_RX_DESC - 1);

  e1000_write_reg(e1000_dev, E1000_REG_TDBAL,
                  (uint32_t)e1000_dev->tx_descs_phys);
  e1000_write_reg(e1000_dev, E1000_REG_TDBAL + 4,
                  (uint32_t)(e1000_dev->tx_descs_phys >> 32));
  e1000_write_reg(e1000_dev, E1000_REG_TDLEN,
                  E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
  e1000_write_reg(e1000_dev, E1000_REG_TDH, 0);
  e1000_write_reg(e1000_dev, E1000_REG_TDT, 0);

  // 8. Enable Receive and Transmit
  e1000_write_reg(e1000_dev, E1000_REG_RCTL,
                  E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SBP |
                      E1000_RCTL_BSIZE_2048);
  e1000_write_reg(e1000_dev, E1000_REG_TCTL,
                  E1000_TCTL_EN | E1000_TCTL_PSP | (0x10 << 4) | (0x40 << 12));

  // 9. Register interrupt handler
  e1000_dev->irq = pci_read_config_byte(dev->bus, dev->device, dev->func,
                                        PCI_INTERRUPT_LINE);
  register_irq_handler(e1000_dev->irq, e1000_interrupt_handler);

  // 10. Enable interrupts
  e1000_write_reg(
      e1000_dev, E1000_REG_IMS,
      0x1F6DC); // Enable RxDMT, RxO, RxUT, TxDW, TxQE, LSC, SDC, PHY

  // Register with network core
  e1000_dev->net_dev.send_packet = e1000_send_packet;
  e1000_dev->net_dev.receive_packet =
      NULL; // Zero-copy, so packets are handled in interrupt
  net_register_device(&e1000_dev->net_dev);

  // Start DHCP client
  dhcp_client_start();

  // klog(LOG_INFO, "E1000: Device initialized and registered successfully.");
  return 0;
}

static void e1000_detach(device_t *dev) {
  (void)dev; // Suppress unused parameter warning
  klog(LOG_INFO, "E1000: Detaching driver from device.");
  // Free resources, disable device, etc.
}

static driver_t e1000_driver = {.name = "e1000_ethernet",
                                .probe = e1000_probe,
                                .attach = e1000_attach,
                                .detach = e1000_detach,
                                .next = NULL};

void e1000_driver_init() {
  deviceman_register_driver(&e1000_driver);
  klog(LOG_INFO, "E1000 driver registered with Device Manager.");
}