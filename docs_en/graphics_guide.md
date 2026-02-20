# Graphics Application Development in KyroOS

## 1. Introduction to KyroOS Graphics Architecture
KyroOS provides a foundational graphics layer primarily through direct framebuffer access. Userspace applications can interact with the system's display hardware by obtaining information about the framebuffer and directly manipulating its memory.

### Key Components:
*   **Framebuffer Access:** The kernel exposes framebuffer information to userspace applications via the `SYS_GFX_GET_FB_INFO` system call.
*   **Native Graphics Primitives:** The `kyroos_gfx` library (userspace/lib/kyroos_gfx) provides basic functions for drawing, such as `gfx_draw_pixel` and `gfx_draw_rect`.

## 2. Low-Level Graphics Primitives (KyroOS Native)

Developing graphics applications natively in KyroOS involves directly utilizing the framebuffer and the `kyroos_gfx` library.

### Obtaining Framebuffer Information:
Applications can retrieve details about the framebuffer by calling `gfx_get_fb_info`, which wraps `SYS_GFX_GET_FB_INFO`. The information is returned in a `user_fb_info_t` structure:

```c
typedef struct {
    uint32_t width;   // Width of the framebuffer in pixels
    uint32_t height;  // Height of the framebuffer in pixels
    uint32_t pitch;   // Number of bytes per line (stride)
    uint32_t bpp;     // Bits per pixel (e.g., 32 for ARGB)
    void* buffer;     // Pointer to the framebuffer memory
} user_fb_info_t;

// Example usage:
user_fb_info_t fb_info;
if (gfx_get_fb_info(&fb_info) == 0) {
    // Successfully obtained framebuffer info
    // You can now access fb_info.buffer to draw pixels
} else {
    // Error getting framebuffer info
}
```

### Direct Pixel Drawing:
Once you have the `fb_info.buffer` pointer, you can directly write pixel data to it. The pixel format typically depends on `fb_info.bpp` (e.g., 32 bpp might be 0xAARRGGBB).

```c
// Example: Drawing a single red pixel at (x, y) for 32bpp
void draw_pixel(int x, int y, uint32_t color, user_fb_info_t* fb_info) {
    if (x >= 0 && x < fb_info->width && y >= 0 && y < fb_info->height) {
        uint32_t* pixel_ptr = (uint32_t*)((uint8_t*)fb_info->buffer + y * fb_info->pitch + x * (fb_info->bpp / 8));
        *pixel_ptr = color;
    }
}
```

### Basic Shapes with `kyroos_gfx`:
The `kyroos_gfx` library provides higher-level functions to draw shapes. Refer to the `userspace/lib/kyroos_gfx/` directory for available functions like `gfx_draw_rect`.

## 3. Integrating High-Level Graphics Libraries (Vulkan, OpenGL)

Integrating advanced graphics APIs like Vulkan or OpenGL into KyroOS presents significant challenges due to their dependency on specific hardware, kernel drivers, and a robust display server/windowing system. KyroOS, as a custom operating system, does not provide these layers by default.

### Understanding the Challenge:
*   **Hardware Abstraction:** Vulkan and OpenGL are designed to abstract GPU hardware. They require drivers (kernel-mode and userspace components) specific to a particular GPU.
*   **Windowing System Integration (WSI):** These APIs need to render to a display surface managed by a windowing system (e.g., Wayland, Xorg, EGL, GLX, WGL). KyroOS currently lacks such a system.
*   **Memory Management:** Sophisticated graphics APIs perform complex memory management, often requiring direct memory access to GPU resources, which depends on kernel support.

### Approach 1: Software Rasterization
This is the most feasible approach for initial integration.
*   **Concept:** Instead of using a GPU, graphics commands are processed and rendered entirely in software on the CPU. The resulting image is then transferred (blipped) to the KyroOS framebuffer.
*   **Examples:** Libraries like Mesa's Softpipe renderer, TinyGL, or custom CPU-based renderers.
*   **Pros:** Highly portable, requires no specific GPU drivers, easier to integrate with KyroOS's existing framebuffer.
*   **Cons:** Very slow, CPU-intensive, unsuitable for complex 3D graphics or high frame rates.
*   **Integration:** A software renderer would draw into a CPU-side buffer. This buffer would then be copied to `user_fb_info_t.buffer` using `memcpy` or a similar blitting function.

### Approach 2: Custom GPU Driver Development
This is a highly complex, long-term endeavor.
*   **Concept:** Develop a kernel-mode driver for a specific GPU (e.g., Intel iGPU, AMD APU, NVIDIA discrete GPU). This driver would expose an interface for userspace to program the GPU.
*   **Challenges:** Extremely complex, hardware-specific, requires deep knowledge of GPU architecture and kernel programming. This is typically a multi-year project for a team.
*   **Userspace Interface:** Once a driver exists, userspace libraries for Vulkan/OpenGL would need to communicate with it, possibly through custom `ioctl` calls or memory-mapped regions.

### Approach 3: Porting a Display Server/Windowing System
*   **Concept:** Port an existing open-source display server (e.g., Wayland, Xorg, or components of Android's SurfaceFlinger) to KyroOS. This server would manage windows, input, and present rendered frames from applications to the framebuffer.
*   **Challenges:** Enormous effort. Requires a robust Inter-Process Communication (IPC) system, comprehensive memory management, process scheduling, and event handling within the KyroOS kernel.
*   **Vulkan/OpenGL Integration:** Once a display server is available, standard WSI components (e.g., Wayland EGL/Vulkan WSI backend) could potentially be ported to integrate Vulkan/OpenGL applications.

### Specifics for Vulkan/OpenGL:

*   **Vulkan:**
    *   Requires a `VkInstance`, `VkSurface`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, `VkSwapchain`, etc.
    *   The `VkSurface` and `VkSwapchain` creation depend on a WSI (Window System Integration) extension (e.g., `VK_KHR_surface`, `VK_KHR_wayland_surface`, `VK_KHR_xlib_surface`).
    *   KyroOS would need a custom WSI implementation that bridges Vulkan's surface requirements with KyroOS's framebuffer. This custom WSI would be a userspace library that interacts with KyroOS's kernel graphics interface.

*   **OpenGL:**
    *   Requires an OpenGL context and a render surface. On Linux, this is typically provided by EGL or GLX.
    *   EGL/GLX would need to be ported or a custom implementation created for KyroOS. This implementation would manage contexts, surfaces, and interact with the KyroOS framebuffer.

## 4. Practical Example (Basic `kyroos_gfx` usage)

Here's a simple program to draw a colored rectangle to the screen using KyroOS's native graphics primitives.

```c
#include <kyroolib.h> // For print, exit, gfx_get_fb_info
#include <stdint.h>   // For uint32_t
#include <stddef.h>   // For size_t
#include <string.h>   // For memset (if available in kyroolib or custom libc)

// Assume user_fb_info_t is defined in kyroolib.h or a similar header
// typedef struct {
//     uint32_t width;
//     uint32_t height;
//     uint32_t pitch;
//     uint32_t bpp;
//     void* buffer;
// } user_fb_info_t;


// Function to draw a filled rectangle
void gfx_draw_rect(int x, int y, int width, int height, uint32_t color, user_fb_info_t* fb_info) {
    if (!fb_info || !fb_info->buffer) return;

    int bytes_per_pixel = fb_info->bpp / 8;

    for (int j = 0; j < height; ++j) {
        int current_y = y + j;
        if (current_y < 0 || current_y >= fb_info->height) continue;

        for (int i = 0; i < width; ++i) {
            int current_x = x + i;
            if (current_x < 0 || current_x >= fb_info->width) continue;

            uint32_t* pixel_ptr = (uint32_t*)((uint8_t*)fb_info->buffer + current_y * fb_info->pitch + current_x * bytes_per_pixel);
            *pixel_ptr = color;
        }
    }
}


int main() {
    user_fb_info_t fb_info;

    print("Attempting to get framebuffer info...\n");
    if (gfx_get_fb_info(&fb_info) != 0) {
        print("Error: Could not get framebuffer info. Exiting.\n");
        exit(1);
    }
    print("Framebuffer info obtained.\n");
    print("Drawing a red rectangle...\n");

    // Draw a red rectangle in the center
    int rect_width = fb_info.width / 4;
    int rect_height = fb_info.height / 4;
    int rect_x = (fb_info.width - rect_width) / 2;
    int rect_y = (fb_info.height - rect_height) / 2;
    uint32_t red = 0x00FF0000; // ARGB format

    gfx_draw_rect(rect_x, rect_y, rect_width, rect_height, red, &fb_info);

    print("Rectangle drawn. Exiting successfully.\n");
    exit(0);
    return 0;
}
```
This example assumes a `gfx_draw_rect` implementation or a basic pixel drawing function, and `memset` in libc for buffer clearing if needed. For this guide, I've provided a simple `gfx_draw_rect` implementation for demonstration.
