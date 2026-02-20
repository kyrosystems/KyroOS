# Writing Graphics Applications for KyroOS

KyroOS offers a flexible approach to developing graphics applications, ranging from direct framebuffer access for maximum control and performance, to leveraging hardware GPU acceleration through standardized APIs like Vulkan, OpenGL, and OpenCL. This document outlines the main approaches and concepts for creating graphics applications and games in KyroOS.

## 1. Direct Framebuffer Access

The simplest and most direct way to render graphics in KyroOS is through direct framebuffer access. The kernel provides the application with direct access to video memory, which eliminates the overhead of context switching and interaction with a graphics server.

### Obtaining Framebuffer Information

To begin, an application must query the current framebuffer information using the `SYS_GFX_GET_FB_INFO` system call. This system call will map the physical pages of the framebuffer directly into your process's virtual address space and return a structure with its parameters.

```c
#include <syscall.h>
#include <kprintf.h>
#include <stddef.h>  // For NULL

// Define a structure similar to gfx_framebuffer_info_t from the kernel,
// for user-space use.
// Ensure it matches the definition in src/include/gfx_driver.h or syscall.h
typedef struct {
    void* address;
    uint64_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t memory_model;
} userspace_fb_info_t;

int main() {
    userspace_fb_info_t fb_info;
    
    // Call the system call to get framebuffer information
    // SYS_GFX_GET_FB_INFO is a system call constant defined in src/include/syscall.h
    int ret = syscall(SYS_GFX_GET_FB_INFO, (uintptr_t)&fb_info, 0, 0, 0, 0);

    if (ret == 0) {
        kprintf("Framebuffer Info:\n");
        kprintf("  Address: 0x%p\n", fb_info.address);
        kprintf("  Resolution: %lu x %lu\n", (uint64_t)fb_info.width, (uint64_t)fb_info.height);
        kprintf("  Pitch: %lu bytes\n", fb_info.pitch);
        kprintf("  BPP: %u\n", fb_info.bpp);

        // Example of drawing a red pixel in the top-left corner
        if (fb_info.address != NULL && fb_info.width > 0 && fb_info.height > 0) {
            uint32_t* pixel_buffer = (uint32_t*)fb_info.address;
            // Calculate offset for pixel (0,0)
            // With bpp=32, each pixel is 4 bytes (uint32_t)
            // pitch is bytes per row, so divide by sizeof(uint32_t)
            uint64_t pixel_offset = (0 * (fb_info.pitch / sizeof(uint32_t))) + 0;
            pixel_buffer[pixel_offset] = 0x00FF0000; // Red color (AARRGGBB)
            kprintf("Drawn a red pixel at (0,0).\n");
        }
    } else {
        kprintf("Failed to get framebuffer info. Error: %d\n", ret);
    }

    // The application should continue to run or wait for other events
    // to maintain the rendered state.
    while (1) {
        // Placeholder to prevent the application from exiting immediately
        syscall(SYS_YIELD, 0, 0, 0, 0, 0); 
    }

    return 0;
}
```

### Rendering Basics

After obtaining the framebuffer address, you can directly write data to this memory. The pixel format is determined by `bpp` (bits per pixel) and `memory_model`. For the most common 32-bit framebuffers (bpp=32), pixels are usually represented in ARGB or XRGB format (0xAARRGGBB), where AA is the alpha channel, RR is red, GG is green, and BB is blue.

To prevent visual artifacts (tearing, flickering), it is recommended to implement your own double or triple buffering in user space if the kernel does not provide a sufficiently fast `fb_flush` for your task.

## 2. Using Hardware APIs: Vulkan, OpenGL, OpenCL

KyroOS lays the groundwork for leveraging hardware GPU acceleration through modern graphics APIs. This is achieved through a modular driver architecture, where the kernel provides a generic interface (`gfx_driver_interface_t`) for interacting with GPU hardware.

### Architecture Overview

1.  **Generic Driver Interface (`gfx_driver.h`):** Defines a set of abstract functions (e.g., `get_capabilities`, `allocate_gpu_memory`, `submit_command_buffer`) that must be implemented by each specific GPU driver.
2.  **Vendor-Specific Drivers (e.g., `amdgpu`):** Implement the `gfx_driver_interface_t`, providing low-level interaction with GPU hardware (MMIO, BARs, registers, proprietary command formats).
3.  **User-Space Libraries:** In the future, user-space libraries (e.g., `libvulkan.so`, `libopengl.so`, `libopencl.so`) will be created. These will use KyroOS system calls to interact with the kernel's active `gfx_driver_interface_t`.

### Application Development Process (Conceptual)

To develop an application using hardware acceleration, you will need:

1.  **GPU Discovery and Interface Request:**
    *   In the future, KyroOS will provide a system call to obtain the `gfx_driver_interface_t` of the active GPU.
    *   The application will be able to query GPU capabilities (`get_capabilities`) to determine support for Vulkan, OpenGL, OpenCL.

2.  **API Context Management:**
    *   User-space libraries (Vulkan Loader, Mesa/Gallium3D for OpenGL/OpenCL) will be responsible for creating and managing contexts for these APIs.
    *   They will use the kernel's `gfx_driver_interface_t` for allocating GPU memory (`allocate_gpu_memory`), submitting commands (`submit_command_buffer`), and managing the display.

3.  **GPU Memory Allocation:**
    *   For textures, vertex buffers, index buffers, and other data requiring placement in video memory, applications will use the APIs of the respective graphics libraries (Vulkan/OpenGL/OpenCL), which, in turn, will call the kernel's `allocate_gpu_memory`.

4.  **Command Submission:**
    *   Generated command buffers (e.g., Vulkan Command Buffers, OpenGL Display Lists) will be submitted for execution on the GPU via the kernel's `submit_command_buffer`.

**Important:** The full implementation of user-space Vulkan, OpenGL, and OpenCL libraries is a very complex and extensive task that will evolve in KyroOS gradually. Currently, the architectural foundation is laid in the kernel.

## 3. Game Development in KyroOS

Game development in KyroOS can utilize both direct framebuffer access for simple 2D games and hardware GPU acceleration for more complex 2D/3D projects.

### General Principles

*   **Game Loop:** Each game will run in an infinite loop that includes:
    *   Processing input (keyboard, mouse events).
    *   Updating game state (logic, physics).
    *   Rendering a new frame.
*   **Input Handling:**
    *   Use the `SYS_INPUT_POLL_EVENT` system call to receive events from the keyboard and mouse from the kernel's centralized event queue.
    *   The application itself is responsible for rendering the cursor (e.g., a small sprite) at the correct location in the framebuffer.
    *   Upon receiving an `EVENT_MOUSE_MOVE` event, the application should redraw its cursor at the new coordinates. This can be done by saving the background image, drawing the cursor, and then restoring the background on the next movement.
*   **Rendering:**
    *   **Simple 2D Games:** Can directly write pixels to the framebuffer.
    *   **Complex 2D/3D Games:** Will use high-level graphics APIs (Vulkan/OpenGL) through user-space libraries for efficient rendering of complex graphics.
*   **Sound:** KyroOS will provide an API for sound playback that will use kernel audio drivers (e.g., AC97) to output audio streams.

### Mouse Cursor

For applications that require displaying a mouse cursor:
1.  The KyroOS kernel provides raw mouse movement events via `SYS_INPUT_POLL_EVENT`.
2.  The application itself is responsible for drawing the cursor (e.g., a small sprite) at the correct location in the framebuffer.
3.  Upon receiving an `EVENT_MOUSE_MOVE` event, the application should redraw its cursor at the new coordinates. This can be done by saving the background image, drawing the cursor, and then restoring the background on the next movement.

For example:

```c
// Example code for handling mouse movement in a game loop
void handle_mouse_move(int32_t dx, int32_t dy) {
    // Update cursor position
    static int32_t mouse_x = 0;
    static int32_t mouse_y = 0;

    // First, erase the old cursor (restore background)
    // draw_background_at(mouse_x, mouse_y, CURSOR_WIDTH, CURSOR_HEIGHT);

    mouse_x += dx;
    mouse_y += dy;

    // Confine cursor to screen bounds
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    // (needs fb_info.width and fb_info.height)

    // Draw the new cursor
    // draw_cursor_at(mouse_x, mouse_y, CURSOR_COLOR);
}

// In the main game loop:
// event_t ev;
// while (syscall(SYS_INPUT_POLL_EVENT, (uintptr_t)&ev, 0, 0, 0, 0) == 0) {
//     if (ev.type == EVENT_MOUSE_MOVE) {
//         handle_mouse_move(ev.mouse.dx, ev.mouse.dy);
//     }
//     // ... handle other events
// }
```

This architecture provides application developers with full control over how their graphics and input interact with the system, allowing for the creation of both simple and high-performance interactive applications.
