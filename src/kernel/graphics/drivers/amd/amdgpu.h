#ifndef AMDGPU_H
#define AMDGPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <gfx_driver.h> // Generic graphics driver interface
#include <driver.h>     // For driver_t
#include <deviceman.h>  // For device_t

// Forward declaration of AMD-specific device structure
struct amdgpu_device_data;

// Function to initialize and register the AMD GPU driver
void amdgpu_driver_init(void);

// External reference to the AMDGPU driver interface implementation
extern gfx_driver_interface_t amdgpu_gfx_interface;

#endif // AMDGPU_H
