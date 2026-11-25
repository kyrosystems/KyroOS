#include "deviceman.h"
#include "log.h"
#include "heap.h" // For kmalloc
#include <stddef.h> // for NULL
#include <string.h> // for memset, strncpy

// Linked list of registered drivers
static driver_t* registered_drivers = NULL;
// Linked list of discovered devices
static device_t* discovered_devices = NULL;

void deviceman_init() {
    registered_drivers = NULL;
    discovered_devices = NULL;
    klog(LOG_INFO, "Device Manager initialized.");
}

void deviceman_register_driver(driver_t* driver) {
    if (!driver) return;

    driver->next = registered_drivers;
    registered_drivers = driver;
    klog(LOG_INFO, "Driver registered.");
}

void deviceman_register_device(device_t* device) {
    if (!device) return;

    device->next = discovered_devices;
    discovered_devices = device;
    klog(LOG_INFO, "Device registered.");
}

void deviceman_probe_devices() {
    klog(LOG_INFO, "Probing devices...");
    device_t* dev = discovered_devices;
    while (dev) {
        driver_t* drv = registered_drivers;
        while (drv) {
            if (drv->probe(dev)) { // Driver probes successfully
                klog(LOG_INFO, "Found a matching driver for device.");
                if (drv->attach(dev) == 0) { // Driver attaches successfully
                    dev->driver = drv;
                    klog(LOG_INFO, "Driver attached to device.");
                    break; // Move to next device
                } else {
                    klog(LOG_WARN, "Driver failed to attach to device.");
                }
            }
            drv = drv->next;
        }
        if (!dev->driver) {
            klog(LOG_WARN, "No suitable driver found for device.");
        }
        dev = dev->next;
    }
    klog(LOG_INFO, "Device probing complete.");
}
