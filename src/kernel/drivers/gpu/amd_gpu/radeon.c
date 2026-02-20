#include "radeon.h"
#include "pci.h"
#include "deviceman.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include <stdbool.h>
#include <stddef.h> // For NULL

// Forward declarations
static int amdgpu_probe(device_t* dev);
static int amdgpu_attach(device_t* dev);
static void amdgpu_detach(device_t* dev);

// AMD GPU Driver structure
static driver_t amdgpu_driver = {
    .name = "AMD GPU Driver",
    .probe = amdgpu_probe,
    .attach = amdgpu_attach,
    .detach = amdgpu_detach,
    .next = NULL
};

// Function to read BARs and their sizes
// Returns the base address and fills size. Handles 32-bit and 64-bit BARs.
static uint64_t pci_get_bar_info(uint8_t bus, uint8_t device, uint8_t func, uint8_t bar_offset, uint64_t *size_out) {
    uint32_t bar_val = pci_read_config_dword(bus, device, func, bar_offset);
    uint64_t base_addr = 0;
    uint64_t size = 0;

    // Save original BAR value
    uint32_t original_bar = bar_val;

    // Write all 1s to BAR to get size
    pci_write_config_dword(bus, device, func, bar_offset, 0xFFFFFFFF);
    uint32_t bar_size_val = pci_read_config_dword(bus, device, func, bar_offset);
    
    // Restore original BAR value
    pci_write_config_dword(bus, device, func, bar_offset, original_bar);

    if (bar_val == 0) { // BAR not implemented
        if (size_out) *size_out = 0;
        return 0;
    }

    if ((bar_val & 0x01) == 0) { // Memory-mapped BAR
        uint8_t type = (bar_val >> 1) & 0x03; // Type bits 2:1
        if (type == 0x02) { // 64-bit BAR
            uint32_t bar_val_high = pci_read_config_dword(bus, device, func, bar_offset + 4);
            uint32_t bar_size_val_high = pci_read_config_dword(bus, device, func, bar_offset + 4);
            
            base_addr = ((uint64_t)bar_val_high << 32) | (bar_val & ~0xF);
            size = ~(((uint64_t)bar_size_val_high << 32) | (bar_size_val & ~0xF)) + 1;
            // The 64-bit BAR takes up two successive DWORDs, so the next BAR is skipped.
            // This needs to be handled by the caller, or this function could return an indicator.
            // For now, caller needs to be aware.
        } else { // 32-bit BAR
            base_addr = bar_val & ~0xF;
            size = ~(bar_size_val & ~0xF) + 1;
        }
    } else { // I/O Space BAR (not typical for GPU MMIO/FB)
        base_addr = bar_val & ~0x3;
        size = ~(bar_size_val & ~0x3) + 1;
        klog(LOG_WARN, "AMDGPU: Found I/O BAR at offset 0x%x, this is unusual for GPU memory regions.", bar_offset);
    }
    
    if (size_out) *size_out = size;
    return base_addr;
}


// Probe function: check if the device is an AMD GPU
static int amdgpu_probe(device_t* dev) {
    if (dev->vendor_id == PCI_VENDOR_ID_AMD) {
        // Check for specific AMD GPU device IDs (Polaris, RDNA, etc.)
        switch (dev->device_id) {
            case PCI_DEVICE_ID_POLARIS10_RX470_480_570_580_590:
            case PCI_DEVICE_ID_POLARIS11_RX460_550_560:
            case PCI_DEVICE_ID_NAVI10_RX5700_XT:
            case PCI_DEVICE_ID_NAVI21_RX6800_XT_6900_XT:
                klog(LOG_INFO, "AMDGPU: Found supported AMD GPU (DeviceID: 0x%x)", dev->device_id);
                return 1; // Supported AMD GPU
            default:
                klog(LOG_DEBUG, "AMDGPU: Found unsupported AMD device (DeviceID: 0x%x)", dev->device_id);
                return 0; // Unsupported AMD device
        }
    }
    return 0; // Not an AMD device
}

// Attach function: initialize the AMD GPU
static int amdgpu_attach(device_t* dev) {
    klog(LOG_INFO, "AMDGPU: Attaching to AMD GPU (VendorID: 0x%x, DeviceID: 0x%x)",
         dev->vendor_id, dev->device_id);

    radeon_device_t *rdev = (radeon_device_t*)kmalloc(sizeof(radeon_device_t));
    if (!rdev) {
        klog(LOG_ERROR, "AMDGPU: Failed to allocate radeon_device_t for GPU 0x%x.", dev->device_id);
        return -1;
    }
    memset(rdev, 0, sizeof(radeon_device_t));
    memcpy(&rdev->pci_dev, dev, sizeof(device_t)); // Copy generic device info

    // Store common PCI info
    rdev->chip_id = dev->device_id;
    strncpy(rdev->pci_dev.name, "AMD GPU", sizeof(rdev->pci_dev.name) - 1); // Specific name
    rdev->pci_dev.driver = &amdgpu_driver; // Set the driver for this device instance

    // Read BARs
    uint64_t bar_size;
    rdev->mmio_base = pci_get_bar_info(dev->bus, dev->device, dev->func, PCI_BAR0, &bar_size);
    rdev->mmio_size = bar_size;
    klog(LOG_INFO, "AMDGPU: MMIO BAR Base: 0x%lx, Size: 0x%lx", rdev->mmio_base, rdev->mmio_size);

    // Note: Framebuffer BAR (VRAM) is typically BAR2 or BAR0 for some cards.
    // GTT BAR is typically BAR5. This might need more robust detection for different GPUs.
    // For now, assume common mapping.
    rdev->fb_base = pci_get_bar_info(dev->bus, dev->device, dev->func, PCI_BAR2, &bar_size);
    rdev->fb_size = bar_size;
    klog(LOG_INFO, "AMDGPU: FB BAR Base: 0x%lx, Size: 0x%lx", rdev->fb_base, rdev->fb_size);

    rdev->gtt_base = pci_get_bar_info(dev->bus, dev->device, dev->func, PCI_BAR5, &bar_size);
    rdev->gtt_size = bar_size;
    klog(LOG_INFO, "AMDGPU: GTT BAR Base: 0x%lx, Size: 0x%lx", rdev->gtt_base, rdev->gtt_size);
    
    // Read IRQ line
    rdev->irq = pci_read_config_byte(dev->bus, dev->device, dev->func, PCI_INTERRUPT_LINE);
    klog(LOG_INFO, "AMDGPU: IRQ Line: %u", rdev->irq);

    // Enable Memory-Mapped I/O and Bus Mastering
    uint16_t pci_cmd = pci_read_config_word(dev->bus, dev->device, dev->func, PCI_COMMAND);
    pci_cmd |= (1 << 1); // Enable Memory Space
    pci_cmd |= (1 << 2); // Enable Bus Master
    pci_write_config_word(dev->bus, dev->device, dev->func, PCI_COMMAND, pci_cmd);
    rdev->pci_command = pci_cmd;
    klog(LOG_INFO, "AMDGPU: PCI Command register set to 0x%x", pci_cmd);

    dev->private_data = rdev; // Store our radeon_device_t in the generic device_t
    klog(LOG_INFO, "AMDGPU: Attached AMD GPU 0x%x successfully.", dev->device_id);
    return 0; // Success
}

// Detach function (placeholder for now)
static void amdgpu_detach(device_t* dev) {
    if (dev->private_data) {
        kfree(dev->private_data);
        dev->private_data = NULL;
    }
    klog(LOG_INFO, "AMDGPU: Detached AMD GPU (DeviceID: 0x%x).", dev->device_id);
}

// Initialization function for the AMD GPU driver
void amdgpu_driver_init() {
    deviceman_register_driver(&amdgpu_driver);
    klog(LOG_INFO, "AMDGPU driver registered with Device Manager.");
}
