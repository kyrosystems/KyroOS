#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include "driver.h" // For device_t

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI Configuration Space Register Offsets
#define PCI_VENDOR_ID         0x00
#define PCI_DEVICE_ID         0x02
#define PCI_COMMAND           0x04
#define PCI_STATUS            0x06
#define PCI_REVISION_ID       0x08
#define PCI_PROG_IF           0x09
#define PCI_SUBCLASS          0x0A
#define PCI_CLASS             0x0B
#define PCI_CACHE_LINE_SIZE   0x0C
#define PCI_LATENCY_TIMER     0x0D
#define PCI_HEADER_TYPE       0x0E
#define PCI_BIST              0x0F

// PCI Base Address Registers (BARs)
#define PCI_BAR0              0x10
#define PCI_BAR1              0x14
#define PCI_BAR2              0x18
#define PCI_BAR3              0x1C
#define PCI_BAR4              0x20
#define PCI_BAR5              0x24

#define PCI_INTERRUPT_LINE    0x3C


void pci_init(); // Initializes the PCI bus scanning
void pci_check_all_buses();

// Helper to construct PCI address
uint32_t pci_get_addr(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);

// Read from PCI configuration space
uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);

// Write to PCI configuration space
void pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t val);
void pci_write_config_word(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint16_t val);
void pci_write_config_byte(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint8_t val);


// Get PCI device info (for example driver to use)
uint16_t pci_get_vendor_id(uint8_t bus, uint8_t device, uint8_t func);
uint16_t pci_get_device_id(uint8_t bus, uint8_t device, uint8_t func);
uint8_t pci_get_class(uint8_t bus, uint8_t device, uint8_t func);
uint8_t pci_get_subclass(uint8_t bus, uint8_t device, uint8_t func);

#endif // PCI_H