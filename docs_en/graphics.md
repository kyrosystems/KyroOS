# 11. Graphics Subsystem

The graphics subsystem in KyroOS is designed with a simple and direct model, providing user applications with direct access to hardware. The kernel lacks complex components such as a graphics server or window compositor.

## 11.1. Framebuffer and Access Model

The foundation of the entire graphics subsystem is a **linear framebuffer**.

### Initialization

During boot, the kernel receives information about the video mode set by the firmware from the Limine bootloader. This information includes:
-   Physical address of the framebuffer start.
-   Screen resolution (width and height in pixels).
-   `pitch` (number of bytes per scanline).
-   Color depth (`bpp` - bits per pixel).

Based on this data, the low-level `fb.c` driver is initialized.

### Double Buffering

To prevent flickering and image tearing during rendering, the `fb.c` driver implements **double buffering**:
1.  Upon initialization, a shadow buffer (`backbuffer`) of the same size as the visible framebuffer is created in kernel memory.
2.  All low-level drawing operations (`fb_put_pixel`, `fb_draw_rect`, `fb_draw_char`) are performed on this shadow buffer.
3.  After completing frame generation, the `fb_flush()` function is called, which atomically copies the entire contents of the `backbuffer` to the visible framebuffer.

### User Space Access Model

KyroOS does not use a client-server model (like X11) for graphics. Instead, it provides user applications with **direct access to video memory**:
1.  An application calls the `SYS_GFX_GET_FB_INFO` system call.
2.  In response, the kernel maps the physical pages of the *entire visible framebuffer* directly into the calling process's virtual address space.
3.  The application receives the screen resolution and the virtual address where the framebuffer is now accessible.
4.  From this point, the application can draw anything it wants by simply writing data to this memory region.

**Advantages:**
-   Maximum performance, as there are no intermediaries or context switches during rendering.

**Disadvantages:**
-   Lack of protection: any application can corrupt any part of the screen.
-   All logic for rendering widgets, window management, and composition must be implemented in user-space libraries.

### GPU Acceleration

Currently, **GPU hardware acceleration is not used**. All rendering operations are performed by the CPU in software.

## 11.2. Windowing System

KyroOS **does not have a kernel-level windowing system**. The kernel has no concepts of "window", "widget", or "desktop".

### Internal GUI Toolkit

Within the kernel code (`gui.c`), there is a rudimentary graphics toolkit. It allows creating simple windows and rendering them. However, this component is **not accessible from user space** and is used exclusively for internal kernel purposes (e.g., debug screens or future TUI installers running in kernel mode).

## 11.3. Input Devices

Interaction with input devices is completely separate from the rendering process.
1.  **Drivers:** Keyboard and mouse drivers are interrupt (IRQ) handlers.
2.  **Event Queue:** When receiving data from a device (key press, mouse movement), the driver does not draw anything on the screen. Instead, it forms an `event_t` structure and places it into a global, centralized **event queue**.
3.  **User Space Polling:** Graphical applications, in their main loop, periodically call the `SYS_INPUT_POLL_EVENT` system call to retrieve the next event from the queue.
4.  **Reaction:** Upon receiving an event (e.g., `EVENT_MOUSE_MOVE`), the application decides how to react (e.g., redraw the cursor at the new position).

## 11.4. OpenGL & Mesa Compatibility (`libgl`)

KyroOS includes a freestanding software-rasterized OpenGL 1.1 / Mesa compatible library (`userspace/lib/libgl/gl.h`):
- **Matrix Stack & Transformations**: `glMatrixMode`, `glPushMatrix`, `glPopMatrix`, `glLoadIdentity`, `glTranslatef`, `glRotatef`, `glScalef`.
- **Primitives**: `glBegin`/`glEnd` for points, lines, triangles, quads, and polygons.
- **Lighting & Materials**: Normals, directional lighting, diffuse & ambient shading.
- **Demos**: `gears` (`/bin/gears`) — the classic 3D spinning gear demo showcasing 3D depth-buffered polygon rendering.

## 11.5. Vulkan 1.1 Software ICD (`libvulkan`)

KyroOS provides a Vulkan 1.1 compliant software ICD (`userspace/lib/libvulkan/vulkan.h`):
- **Core Objects**: `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`, `VkCommandPool`, `VkCommandBuffer`, `VkFramebuffer`.
- **Pipeline & Shaders**: Graphics pipeline creation, vertex layout descriptors, viewport and scissor state.
- **Drawing Commands**: `vkCmdBeginRenderPass`, `vkCmdBindPipeline`, `vkCmdDraw`, `vkQueueSubmit`.
- **Demos**: `vkcube` (`/bin/vkcube`) — a standalone 3D colored cube rotating in real-time, rendered purely through the Vulkan API.

## 11.6. Graphical Desktop Environment (`desktop` / `startx`)

KyroOS includes an optional graphical desktop environment, the **KyroOS Beryllium Desktop Environment**:
- **On-Demand Launch**: The desktop is not launched on boot by default; it is started by running `desktop` or `startx` from the `sh` command prompt. Pressing `Esc` or clicking "Exit to Shell" in the Start menu exits cleanly back to the text console.
- **Window Management**: Supports free window dragging by title bar, taskbar minimization (`_`), maximize/restore (`□`), window close (`X`), soft drop shadows, and active/inactive gradient titlebars.
- **Classic Taskbar**: Positioned at the bottom of the screen featuring the stylized KyroOS Start button (`KyroOS` with the neon cyan emblem), taskbar buttons for open/minimized windows, and a sunken system tray showing real-time clock and network status.
- **Procedural Horizon Wallpaper**: Dynamic azure sky gradient with procedural clouds and green rolling hills rendered entirely in software without CPU performance overhead.
- **Interactive Desktop Icons**: Vector pixel-art desktop shortcuts ("My Computer", "KyroTerm", "Explorer", "Task Manager", "3D Graphics", "Installer").
- **KyroTerm Shell Console**: Interactive terminal window capable of running all built-in commands and external system binaries in `/bin/` with output captured and displayed in the window.
- **Desktop Resource Widget**: An on-desktop monitoring panel showing real-time CPU usage, RAM consumption, hardware GPU vendor/model and VRAM aperture (via `SYS_GET_GPU_INFO`), `eth0` network status, and KyroFS disk health.
