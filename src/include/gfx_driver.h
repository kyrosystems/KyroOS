#ifndef GFX_DRIVER_H
#define GFX_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
struct driver; // From driver.h
struct device; // From deviceman.h

// --- Generic Framebuffer Information ---
typedef struct {
    void* address;
    uint64_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t memory_model; // e.g., RGB, indexed
} gfx_framebuffer_info_t;

// --- Generic Graphics Device Capabilities ---
typedef struct {
    bool has_2d_acceleration;
    bool has_3d_acceleration;
    bool has_vulkan;
    bool has_opencl;
    bool has_opengl;
    // Add more capabilities as needed
} gfx_capabilities_t;

// --- Generic Graphics Driver Interface ---
// This structure defines the contract for any graphics driver in KyroOS.
// Vendor-specific drivers will implement these functions.
typedef struct gfx_driver_interface {
    // Basic device management
    const char* (*get_name)(struct device* dev);
    gfx_capabilities_t (*get_capabilities)(struct device* dev);
    gfx_framebuffer_info_t (*get_framebuffer_info)(struct device* dev);

    // Memory management
    // Example: Allocate GPU-accessible memory
    void* (*allocate_gpu_memory)(struct device* dev, size_t size, uint64_t flags);
    void (*free_gpu_memory)(struct device* dev, void* ptr);

    // Command queue management
    // Example: Submit a command buffer for 2D/3D operations
    int (*submit_command_buffer)(struct device* dev, void* command_buffer, size_t size);

    // Display / Output management
    // Example: Set display mode (if driver directly manages display)
    int (*set_display_mode)(struct device* dev, uint32_t width, uint32_t height, uint8_t bpp);
    
    // VSync control
    void (*wait_for_vsync)(struct device* dev);

    // For Vulkan/OpenCL/GL, these would involve creating contexts,
    // managing shaders, textures, etc., abstracted through this interface.
    // The specific implementation will be in the vendor drivers.

} gfx_driver_interface_t;

// A graphics driver entry point for device manager
// This links the generic PCI driver (struct driver) with the graphics-specific interface.
typedef struct gfx_driver {
    struct driver* pci_driver; // The underlying PCI driver for the GPU
    gfx_driver_interface_t* interface; // The specific implementation of the graphics interface
} gfx_driver_t;

// Function to register a graphics driver with the graphics subsystem
void gfx_register_driver(gfx_driver_t* driver);

#endif // GFX_DRIVER_H
