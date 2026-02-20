#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <gfx_driver.h> // For gfx_driver_t

// Function to register a graphics driver with the graphics subsystem
void gfx_register_driver(gfx_driver_t* driver);

// Function to get the active graphics driver
gfx_driver_t* gfx_get_active_driver(void);

#endif // GRAPHICS_H
