#include "amdgpu.h"
#include <log.h>
#include <pci.h> // For PCI device identification
#include <vmm.h> // For memory mapping
#include <pmm.h> // For physical memory allocation (kmalloc)
#include <mutex.h> // For synchronization
#include <kstring.h> // For memset, memcpy
#include <heap.h> // For kmalloc, kfree
#include <graphics/graphics.h> // For gfx_register_driver

// --- AMDGPU Device Specific Data (Placeholder) ---
// This structure would hold all the state specific to an AMD GPU,
// such as MMIO base, framebuffer info, command queues, etc.
typedef struct amdgpu_device_data {
    struct device* pci_device; // Pointer to the generic PCI device
    // PCI BARs information
    uintptr_t mmio_base;
    size_t mmio_size;
    uintptr_t framebuffer_base;
    size_t framebuffer_size;

    // Add more AMD-specific hardware details here
    // e.g., command buffers, ring buffers, registers, etc.

    mutex_t lock; // Example: Mutex for synchronizing access to device

} amdgpu_device_data_t;

// --- Forward Declarations for GFX Interface Functions ---
static const char* amdgpu_get_name(struct device* dev);
static gfx_capabilities_t amdgpu_get_capabilities(struct device* dev);
static gfx_framebuffer_info_t amdgpu_get_framebuffer_info(struct device* dev);
static void* amdgpu_allocate_gpu_memory(struct device* dev, size_t size, uint64_t flags);
static void amdgpu_free_gpu_memory(struct device* dev, void* ptr);
static int amdgpu_submit_command_buffer(struct device* dev, void* command_buffer, size_t size);
static int amdgpu_set_display_mode(struct device* dev, uint32_t width, uint32_t height, uint8_t bpp);
static void amdgpu_wait_for_vsync(struct device* dev);

// --- GFX Driver Interface Implementation ---
gfx_driver_interface_t amdgpu_gfx_interface = {
    .get_name = amdgpu_get_name,
    .get_capabilities = amdgpu_get_capabilities,
    .get_framebuffer_info = amdgpu_get_framebuffer_info,
    .allocate_gpu_memory = amdgpu_allocate_gpu_memory,
    .free_gpu_memory = amdgpu_free_gpu_memory,
    .submit_command_buffer = amdgpu_submit_command_buffer,
    .set_display_mode = amdgpu_set_display_mode,
    .wait_for_vsync = amdgpu_wait_for_vsync,
};

// --- AMDGPU Driver Core Functions ---

// Probe function: Called by device manager to check if this driver supports the device
static int amdgpu_probe(struct device* dev) {
    // Directly check for known AMD GPU Vendor ID and Device IDs
    // The device manager should pass a 'device_t' that represents a PCI device.
    if (dev->vendor_id == PCI_VENDOR_ID_AMD) {
        klog(LOG_INFO, "AMDGPU: Found potential AMD GPU (Vendor ID: 0x%x, Device ID: 0x%x)",
             dev->vendor_id, dev->device_id);
        return 1;
    }
    return 0; // Not an AMD GPU or not supported
}

// Attach function: Called by device manager to attach driver to the device
static int amdgpu_attach(struct device* dev) {
    klog(LOG_INFO, "AMDGPU: Attaching driver to device 0x%x:0x%x",
         dev->vendor_id, dev->device_id);

    amdgpu_device_data_t* amd_data = kmalloc(sizeof(amdgpu_device_data_t));
    if (!amd_data) {
        klog(LOG_ERROR, "AMDGPU: Failed to allocate device data.");
        return -1;
    }
    memset(amd_data, 0, sizeof(amdgpu_device_data_t));
    amd_data->pci_device = dev; // Store the generic device pointer
    mutex_init(&amd_data->lock);

    // Store AMD-specific data in the generic device structure
    dev->private_data = amd_data;

    // Enable PCI bus mastering and memory-mapped I/O
    // The PCI information is directly in the 'dev' struct
    pci_write_word(dev->bus, dev->device, dev->func, PCI_COMMAND, // Use dev->bus, dev->device, dev->func
                   pci_read_word(dev->bus, dev->device, dev->func, PCI_COMMAND) |
                   PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER);

    // Read PCI BARs and map MMIO/Framebuffer
    // This is a simplified example. Real drivers would perform extensive BAR parsing and mapping.
    for (int i = 0; i < 6; ++i) { // PCI has 6 BARs
        pci_bar_t bar = pci_get_bar(dev->bus, dev->device, dev->func, i); // Use dev->bus, dev->device, dev->func
        if (bar.address) {
            if (bar.is_memory_space) {
                if (bar.is_prefetchable) {
                    // This could be VRAM (framebuffer) or other MMIO
                    if (!amd_data->framebuffer_base) { // Assume first prefetchable is FB
                        amd_data->framebuffer_base = bar.address;
                        amd_data->framebuffer_size = bar.size;
                        klog(LOG_INFO, "AMDGPU: Framebuffer BAR%d: base=0x%lx, size=0x%lx",
                             i, amd_data->framebuffer_base, amd_data->framebuffer_size);
                        // Map framebuffer
                        for (uintptr_t p = amd_data->framebuffer_base;
                             p < amd_data->framebuffer_base + amd_data->framebuffer_size;
                             p += PAGE_SIZE) {
                            vmm_map_page(vmm_get_current_pml4(), (void*)p, (void*)p, PAGE_PRESENT | PAGE_WRITE);
                        }
                                             for (uintptr_t p = amd_data->mmio_base;
                                                  p < amd_data->mmio_base + amd_data->mmio_size;
                                                  p += PAGE_SIZE) {
                                                 vmm_map_page(vmm_get_current_pml4(), (void*)p, (void*)p, PAGE_PRESENT | PAGE_WRITE);
                                             }                    }
                } else { // Non-prefetchable memory BAR, likely MMIO
                    if (!amd_data->mmio_base) {
                        amd_data->mmio_base = bar.address;
                        amd_data->mmio_size = bar.size;
                        klog(LOG_INFO, "AMDGPU: MMIO BAR%d: base=0x%lx, size=0x%lx",
                             i, amd_data->mmio_base, amd_data->mmio_size);
                        // Map MMIO
                                                  for (uintptr_t p = amd_data->mmio_base;
                                                       p < amd_data->mmio_base + amd_data->mmio_size;
                                                       p += PAGE_SIZE) {
                                                      vmm_map_page(vmm_get_current_pml4(), (void*)p, (void*)p, PAGE_PRESENT | PAGE_WRITE);
                                                  }                    }
                }
            }
        }
    }
    
    // Check if essential bases are found
    if (!amd_data->mmio_base || !amd_data->framebuffer_base) {
        klog(LOG_ERROR, "AMDGPU: Failed to find essential MMIO or Framebuffer BARs.");
        kfree(amd_data);
        return -1;
    }

    klog(LOG_INFO, "AMDGPU: Driver attached successfully.");
    return 0;
}

// Detach function: Called by device manager to detach driver
static void amdgpu_detach(struct device* dev) { // Changed return type to void
    klog(LOG_INFO, "AMDGPU: Detaching driver from device.");
    amdgpu_device_data_t* amd_data = (amdgpu_device_data_t*)dev->private_data;
    if (amd_data) {
        // Unmap memory
        if (amd_data->mmio_base && amd_data->mmio_size) {
            for (uintptr_t p = amd_data->mmio_base;
                 p < amd_data->mmio_base + amd_data->mmio_size;
                 p += PAGE_SIZE) {
                vmm_unmap_page(vmm_get_current_pml4(), (void*)p);
            }
        }
        if (amd_data->framebuffer_base && amd_data->framebuffer_size) {
            for (uintptr_t p = amd_data->framebuffer_base;
                 p < amd_data->framebuffer_base + amd_data->framebuffer_size;
                 p += PAGE_SIZE) {
                vmm_unmap_page(vmm_get_current_pml4(), (void*)p);
            }
        }
        kfree(amd_data);
        dev->private_data = NULL;
    }
}

// --- GFX Interface Function Implementations (minimal for skeleton) ---

static const char* amdgpu_get_name(struct device* dev) {
    // This could return a more specific name based on device ID
    return "AMD Radeon GPU";
}

static gfx_capabilities_t amdgpu_get_capabilities(struct device* dev) {
    gfx_capabilities_t caps = {0};
    caps.has_2d_acceleration = true; // Assume basic 2D for modern GPUs
    caps.has_3d_acceleration = true; // Assume 3D acceleration
    // For now, just indicate readiness for Vulkan/OpenCL/GL, actual support needs implementation
    caps.has_vulkan = true;
    caps.has_opencl = true;
    caps.has_opengl = true;
    return caps;
}

static gfx_framebuffer_info_t amdgpu_get_framebuffer_info(struct device* dev) {
    amdgpu_device_data_t* amd_data = (amdgpu_device_data_t*)dev->private_data;
    gfx_framebuffer_info_t fb_info = {0};

    if (amd_data && amd_data->framebuffer_base) {
        fb_info.address = (void*)amd_data->framebuffer_base;
        // These values would ideally be read from GPU registers after mode setting
        // For a basic setup, we might use values from Limine or assume common
        // VESA modes. This needs to be properly determined by the driver.
        fb_info.width = 1024; // Placeholder
        fb_info.height = 768; // Placeholder
        fb_info.bpp = 32;     // Placeholder
        fb_info.pitch = fb_info.width * (fb_info.bpp / 8); // Placeholder
        fb_info.memory_model = 0; // Unknown/Direct RGB for now
    }
    return fb_info;
}

static void* amdgpu_allocate_gpu_memory(struct device* dev, size_t size, uint64_t flags) {
    klog(LOG_WARN, "AMDGPU: allocate_gpu_memory not yet fully implemented for real GPU memory. Returning NULL.");
    return NULL;
}

static void amdgpu_free_gpu_memory(struct device* dev, void* ptr) {
    klog(LOG_WARN, "AMDGPU: free_gpu_memory not yet implemented.");
}

static int amdgpu_submit_command_buffer(struct device* dev, void* command_buffer, size_t size) {
    klog(LOG_WARN, "AMDGPU: submit_command_buffer not yet implemented.");
    return -1;
}

static int amdgpu_set_display_mode(struct device* dev, uint32_t width, uint32_t height, uint8_t bpp) {
    klog(LOG_WARN, "AMDGPU: set_display_mode not yet implemented.");
    return -1;
}

static void amdgpu_wait_for_vsync(struct device* dev) {
    klog(LOG_WARN, "AMDGPU: wait_for_vsync not yet implemented.");
}


// --- Generic Driver_t structure for Device Manager ---
static driver_t amdgpu_pci_driver = {
    .name = "amdgpu_driver",
    .probe = amdgpu_probe,
    .attach = amdgpu_attach,
    .detach = amdgpu_detach,
};

// --- Entry point for the AMDGPU driver ---
void amdgpu_driver_init(void) {
    klog(LOG_INFO, "AMDGPU: Initializing AMD GPU driver.");

    // Register the PCI driver with the device manager
    deviceman_register_driver(&amdgpu_pci_driver);

    // Create and register the graphics driver interface
    gfx_driver_t gfx_drv = {
        .pci_driver = &amdgpu_pci_driver,
        .interface = &amdgpu_gfx_interface,
    };
    gfx_register_driver(&gfx_drv); 
    klog(LOG_INFO, "AMDGPU: AMD GPU driver registered with Device Manager.");
}