#include "graphics.h"
#include <log.h>
#include <stddef.h> // For NULL
#include <driver.h> // For struct driver

static gfx_driver_t* active_graphics_driver = NULL;

void gfx_register_driver(gfx_driver_t* driver) {
    if (!driver) {
        klog(LOG_ERROR, "GFX: Attempted to register a NULL graphics driver.");
        return;
    }
    if (active_graphics_driver) {
        klog(LOG_WARN, "GFX: A graphics driver is already active. Overwriting.");
    }
    active_graphics_driver = driver;
    klog(LOG_INFO, "GFX: Graphics driver '%s' registered as active.", driver->pci_driver->name);
}

gfx_driver_t* gfx_get_active_driver(void) {
    return active_graphics_driver;
}
