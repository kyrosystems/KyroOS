#include "pci.h"
#include "deviceman.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "port_io.h"
#include <string.h> // For memset

// Function to construct PCI address
inline uint32_t pci_get_addr(uint8_t bus, uint8_t device, uint8_t func,
                             uint8_t offset) {
  return (uint32_t)((bus << 16) | (device << 11) | (func << 8) |
                    (offset & 0xFC) | 0x80000000);
}

// Public PCI read functions
uint32_t pci_read_config_dword(uint8_t bus, uint8_t device, uint8_t func,
                               uint8_t offset) {
  outl(PCI_CONFIG_ADDRESS, pci_get_addr(bus, device, func, offset));
  return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_config_word(uint8_t bus, uint8_t device, uint8_t func,
                              uint8_t offset) {
  outl(PCI_CONFIG_ADDRESS, pci_get_addr(bus, device, func, offset));
  return (uint16_t)((inl(PCI_CONFIG_DATA) >> ((offset % 4) * 8)) & 0xFFFF);
}

uint8_t pci_read_config_byte(uint8_t bus, uint8_t device, uint8_t func,
                             uint8_t offset) {
  outl(PCI_CONFIG_ADDRESS, pci_get_addr(bus, device, func, offset));
  return (uint8_t)((inl(PCI_CONFIG_DATA) >> ((offset % 4) * 8)) & 0xFF);
}

// Public PCI write functions
void pci_write_config_dword(uint8_t bus, uint8_t device, uint8_t func,
                            uint8_t offset, uint32_t val) {
  outl(PCI_CONFIG_ADDRESS, pci_get_addr(bus, device, func, offset));
  outl(PCI_CONFIG_DATA, val);
}

void pci_write_config_word(uint8_t bus, uint8_t device, uint8_t func,
                           uint8_t offset, uint16_t val) {
  uint32_t current_val = pci_read_config_dword(bus, device, func, offset & ~3);
  uint32_t mask = 0xFFFF << ((offset % 4) * 8);
  current_val = (current_val & ~mask) | ((uint32_t)val << ((offset % 4) * 8));
  pci_write_config_dword(bus, device, func, offset & ~3, current_val);
}

void pci_write_config_byte(uint8_t bus, uint8_t device, uint8_t func,
                           uint8_t offset, uint8_t val) {
  uint32_t current_val = pci_read_config_dword(bus, device, func, offset & ~3);
  uint32_t mask = 0xFF << ((offset % 4) * 8);
  current_val = (current_val & ~mask) | ((uint32_t)val << ((offset % 4) * 8));
  pci_write_config_dword(bus, device, func, offset & ~3, current_val);
}

// Get PCI device info
uint16_t pci_get_vendor_id(uint8_t bus, uint8_t device, uint8_t func) {
  return pci_read_config_word(bus, device, func, PCI_VENDOR_ID);
}

uint16_t pci_get_device_id(uint8_t bus, uint8_t device, uint8_t func) {
  return pci_read_config_word(bus, device, func, PCI_DEVICE_ID);
}

uint8_t pci_get_class(uint8_t bus, uint8_t device, uint8_t func) {
  return pci_read_config_byte(bus, device, func, PCI_CLASS);
}

uint8_t pci_get_subclass(uint8_t bus, uint8_t device, uint8_t func) {
  return pci_read_config_byte(bus, device, func, PCI_SUBCLASS);
}

static void pci_check_function(uint8_t bus, uint8_t device, uint8_t func) {
  uint16_t vendorID = pci_get_vendor_id(bus, device, func);
  if (vendorID == 0xFFFF)
    return; // Device doesn't exist

  uint16_t deviceID = pci_get_device_id(bus, device, func);
  uint8_t classCode = pci_get_class(bus, device, func);
  uint8_t subclassCode = pci_get_subclass(bus, device, func);
  (void)classCode;    // Suppress unused variable warning
  (void)subclassCode; // Suppress unused variable warning

  device_t *dev = (device_t *)kmalloc(sizeof(device_t));
  if (!dev) {
    klog(LOG_ERROR, "PCI: Failed to allocate device_t.");
    return;
  }
  memset(dev, 0, sizeof(device_t));

  // Populate with basic info
  dev->bus = bus;
  dev->device = device;
  dev->func = func;
  dev->vendor_id = vendorID;
  dev->device_id = deviceID;
  strncpy(dev->name, "PCI Device", sizeof(dev->name) - 1); // Generic name

  char buf[128];
  ksprintf(buf, "PCI: Found device (VendorID: 0x%x, DeviceID: 0x%x)",
           (int)vendorID, (int)deviceID);
  klog(LOG_INFO, "%s", buf);

  deviceman_register_device(dev);
}

static void pci_check_device(uint8_t bus, uint8_t device) {
  uint8_t headerType = pci_read_config_byte(bus, device, 0, PCI_HEADER_TYPE);

  if (pci_get_vendor_id(bus, device, 0) == 0xFFFF)
    return; // Check if device exists before checking functions

  if ((headerType & 0x80) != 0) { // Multi-function device
    for (uint8_t func = 0; func < 8; func++) {
      if (pci_get_vendor_id(bus, device, func) !=
          0xFFFF) { // Check if function exists
        pci_check_function(bus, device, func);
      }
    }
  } else { // Single function device
    pci_check_function(bus, device, 0);
  }
}

void pci_check_all_buses() {
  klog(LOG_INFO, "PCI: Scanning all buses...");
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint8_t device = 0; device < 32; device++) {
      pci_check_device(bus, device);
    }
  }
  klog(LOG_INFO, "PCI: Scan complete.");
}

void pci_init() {

}
