#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

// Forward declaration
struct device;
struct driver;

// --- Device Structure ---
typedef struct device {
    char name[64];
    
    // PCI device info
    uint8_t bus;
    uint8_t device;
    uint8_t func;
    
    uint16_t vendor_id;
    uint16_t device_id;
    
    // ... other device specific information
    struct driver* driver; // Pointer to the driver handling this device
    struct device* next;   // Linked list of devices
    void* private_data;    // Driver-specific data for this device
} device_t;

// --- Driver Structure ---
typedef struct driver {
    char name[64];
    // Function to check if this driver supports the given device
    int (*probe)(device_t* dev);
    // Function to initialize and attach the driver to the device
    int (*attach)(device_t* dev);
    // Function to detach/deinitialize the driver from the device
    void (*detach)(device_t* dev);
    // ... other driver-specific function pointers (e.g., read, write for block devices)
    struct driver* next; // Linked list of drivers
} driver_t;

#endif // DRIVER_H