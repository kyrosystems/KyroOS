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
