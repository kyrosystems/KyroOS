#ifndef RADEON_GPU_H
#define RADEON_GPU_H

#include <stdint.h>
#include "driver.h" // For device_t

// Vendor ID for AMD
#define PCI_VENDOR_ID_AMD 0x1002

// Device IDs for Polaris and RDNA family (examples, more can be added)
// Polaris
#define PCI_DEVICE_ID_POLARIS10_RX470_480_570_580_590 0x67DF
#define PCI_DEVICE_ID_POLARIS11_RX460_550_560        0x67FF

// RDNA1 (Navi 1x)
#define PCI_DEVICE_ID_NAVI10_RX5700_XT             0x731F

// RDNA2 (Navi 2x)
#define PCI_DEVICE_ID_NAVI21_RX6800_XT_6900_XT     0x73BF

// BAR indices for standard PCI
#define RADEON_MMIO_BAR_IDX 0 // Memory-mapped I/O registers
#define RADEON_FB_BAR_IDX   2 // Framebuffer (VRAM)
#define RADEON_GTT_BAR_IDX  5 // GTT (Graphics Translation Table, system memory aperture)

// Radeon-specific device structure
typedef struct radeon_device {
    device_t pci_dev; // Embed the generic PCI device info

    // MMIO Registers Base Address
    uintptr_t mmio_base;
    uint64_t mmio_size; // Size of MMIO region

    // Framebuffer (VRAM) Base Address
    uintptr_t fb_base;
    uint64_t fb_size; // Size of VRAM region

    // GTT (Graphics Translation Table) Base Address
    uintptr_t gtt_base;
    uint64_t gtt_size; // Size of GTT region

    // Interrupt line
    uint8_t irq;

    // Chip ID (e.g., POLARIS10, NAVI10) - from pci_dev.device_id
    uint32_t chip_id; 

    // PCI configuration space command register (to enable/disable certain features)
    uint16_t pci_command; // Cached value

    // Add more GPU-specific registers or state here as needed
    // e.g., ring buffer details, power management, etc.

} radeon_device_t;

// Function declarations for the AMD GPU driver
void amdgpu_driver_init(); // Registers the AMD GPU driver with the device manager

#endif // RADEON_GPU_H
