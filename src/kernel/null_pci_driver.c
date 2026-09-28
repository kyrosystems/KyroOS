#include "driver.h"
#include "deviceman.h"
#include "log.h"
#include "pci.h" // For PCI constants
#include <stddef.h> // For NULL

// --- Null PCI Driver ---
static int null_pci_probe(device_t* dev) {
    // For demonstration, let's say this driver handles a specific QEMU device
    // E.g., QEMU's default VGA controller is Vendor ID 0x1234, Device ID 0x1111
    if (dev->vendor_id == 0x1234 && dev->device_id == 0x1111) {
        klog(LOG_INFO, "Null PCI Driver: Found QEMU VGA device (0x1234:0x1111).");
        return 1; // This driver can handle this device
    }
    // Or a virtio-pci device (example: Virtio network card)
    if (dev->vendor_id == 0x1af4 && (dev->device_id >= 0x1000 && dev->device_id <= 0x103F)) {
        klog(LOG_INFO, "Null PCI Driver: Found Virtio device.");
        return 1;
    }
    return 0; // This driver does not handle this device
}

static int null_pci_attach(device_t* dev) {
    klog(LOG_INFO, "Null PCI Driver: Attached to a device.");
    dev->private_data = NULL; // No specific data for this null driver
    return 0; // Success
}

static void null_pci_detach(device_t* dev) {
    (void)dev; // Suppress unused parameter warning
    klog(LOG_INFO, "Null PCI Driver: Detached from a device.");
    // Free any allocated private_data if necessary
}

static driver_t null_pci_driver = {
    .name = "null_pci_driver",
    .probe = null_pci_probe,
    .attach = null_pci_attach,
    .detach = null_pci_detach,
    .next = NULL
};

void null_pci_driver_init() {
    deviceman_register_driver(&null_pci_driver);
    klog(LOG_INFO, "Null PCI Driver initialized and registered.");
}