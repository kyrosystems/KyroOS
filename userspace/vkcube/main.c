#include <vulkan/vulkan.h>
#include <kyroolib.h>
#include <kyroos_gfx.h>
#include <math.h>
#include <stdbool.h>

// 3D Point and projected 2D Point
typedef struct {
    float x, y, z;
} vec3_t;

typedef struct {
    int x, y;
} point2d_t;

// Cube vertices (-1.0 to 1.0)
static const vec3_t cube_vertices[8] = {
    {-1.0f, -1.0f, -1.0f},
    { 1.0f, -1.0f, -1.0f},
    { 1.0f,  1.0f, -1.0f},
    {-1.0f,  1.0f, -1.0f},
    {-1.0f, -1.0f,  1.0f},
    { 1.0f, -1.0f,  1.0f},
    { 1.0f,  1.0f,  1.0f},
    {-1.0f,  1.0f,  1.0f}
};

// 12 edges connecting vertex indices
static const int cube_edges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Front face
    {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Back face
    {0, 4}, {1, 5}, {2, 6}, {3, 7}  // Connecting edges
};

// Colors for edges
static const uint32_t edge_colors[12] = {
    0xFFFF3333, 0xFFFF3333, 0xFFFF3333, 0xFFFF3333, // Red
    0xFF33FF33, 0xFF33FF33, 0xFF33FF33, 0xFF33FF33, // Green
    0xFF3388FF, 0xFF3388FF, 0xFFFFFF33, 0xFFFF33FF  // Blue / Yellow / Magenta
};

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: vkcube\n");
        print("KyroOS Vulkan 3D Spinning Cube Demo with Real-time Performance HUD.\n");
        print("Controls: 'q' or Esc to exit.\n");
        print_flush();
        return 0;
    }

    print("Initializing Vulkan on KyroOS (press 'q' or Esc to exit)...\n");

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "KyroOS-VkCube",
        .applicationVersion = 1,
        .pEngineName = "KyroVK",
        .engineVersion = 1,
        .apiVersion = 0x00401000
    };

    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info
    };

    VkInstance instance;
    if (vkCreateInstance(&inst_info, NULL, &instance) != VK_SUCCESS) {
        print("vkcube: Failed to create Vulkan instance.\n");
        return 1;
    }

    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(instance, &dev_count, NULL);
    if (dev_count == 0) {
        print("vkcube: No Vulkan physical devices found.\n");
        return 1;
    }

    VkPhysicalDevice phys_dev;
    vkEnumeratePhysicalDevices(instance, &dev_count, &phys_dev);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys_dev, &props);
    print("Vulkan GPU: ");
    print(props.deviceName);
    print("\n");

    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
    };

    VkDevice device;
    if (vkCreateDevice(phys_dev, &dev_info, NULL, &device) != VK_SUCCESS) {
        print("vkcube: Failed to create logical device.\n");
        return 1;
    }

    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);

    VkSwapchainCreateInfoKHR sc_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .imageExtent = { .width = 1024, .height = 768 }
    };

    VkSwapchainKHR swapchain;
    if (vkCreateSwapchainKHR(device, &sc_info, NULL, &swapchain) != VK_SUCCESS) {
        print("vkcube: Failed to create Vulkan swapchain.\n");
        return 1;
    }

    uint32_t screen_w = gfx_get_width();
    uint32_t screen_h = gfx_get_height();
    if (screen_w == 0) screen_w = 1024;
    if (screen_h == 0) screen_h = 768;

    int center_x = (int)(screen_w / 2);
    int center_y = (int)(screen_h / 2);
    float scale = (float)(screen_h / 3);

    VkPresentInfoKHR pres_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .swapchainCount = 1,
        .pSwapchains = &swapchain
    };

    float rot_x = 0.0f;
    float rot_y = 0.0f;
    bool running = true;

    uint64_t last_time = get_ticks();
    uint32_t fps = 60;
    uint32_t frame_count = 0;
    uint64_t fps_timer = last_time;

    gfx_event_t drain_ev;
    while (gfx_poll_event(&drain_ev)) {}

    // Render loop: runs indefinitely until 'q' or Esc is pressed
    while (running) {
        // Poll input events
        gfx_event_t ev;
        memset(&ev, 0, sizeof(ev));
        while (gfx_poll_event(&ev)) {
            if (ev.type == GFX_EVENT_KEY_DOWN) {
                char c = (char)ev.data1;
                if (c == 'q' || c == 'Q' || c == 27 || ev.data1 == 27) {
                    running = false;
                    break;
                }
            }
        }
        if (!running) break;

        // Clear framebuffer (dark slate)
        gfx_clear(0xFF141A22);

        // Header banner
        gfx_draw_rect(0, 0, screen_w, 40, 0xFF1C2430);
        gfx_draw_rect(0, 39, screen_w, 2, 0xFFE03030); // Vulkan Red accent line
        gfx_draw_string(20, 12, "KyroOS Vulkan 3D Spinning Cube", 0xFFFFFFFF, 0);
        gfx_draw_string(screen_w - 220, 12, "[ Press 'q' or Esc to exit ]", 0xFFAAAAAA, 0);

        // Calculate trigonometry
        float rad_x = rot_x * (float)(PI / 180.0);
        float rad_y = rot_y * (float)(PI / 180.0);
        float cx = cosf(rad_x), sx = sinf(rad_x);
        float cy = cosf(rad_y), sy = sinf(rad_y);

        // Transform and project all 8 vertices
        point2d_t proj[8];
        for (int i = 0; i < 8; i++) {
            float vx = cube_vertices[i].x;
            float vy = cube_vertices[i].y;
            float vz = cube_vertices[i].z;

            // Rotate around Y
            float x1 = vx * cy + vz * sy;
            float y1 = vy;
            float z1 = -vx * sy + vz * cy;

            // Rotate around X
            float x2 = x1;
            float y2 = y1 * cx - z1 * sx;
            float z2 = y1 * sx + z1 * cx;

            // Perspective projection
            float z_proj = z2 + 3.2f;
            if (z_proj < 0.1f) z_proj = 0.1f;

            proj[i].x = center_x + (int)((x2 * scale) / z_proj);
            proj[i].y = center_y + (int)((y2 * scale) / z_proj);
        }

        // Draw 12 cube edges
        for (int e = 0; e < 12; e++) {
            int p0 = cube_edges[e][0];
            int p1 = cube_edges[e][1];
            gfx_draw_line(proj[p0].x, proj[p0].y, proj[p1].x, proj[p1].y, edge_colors[e]);
            gfx_draw_line(proj[p0].x + 1, proj[p0].y, proj[p1].x + 1, proj[p1].y, edge_colors[e]);
        }

        // FPS & timing calculations
        uint64_t cur_time = get_ticks();
        uint32_t frame_time_ms = (uint32_t)((cur_time - last_time) * 10);
        if (frame_time_ms == 0) frame_time_ms = 16;
        last_time = cur_time;
        frame_count++;
        if (cur_time - fps_timer >= 100) {
            fps = frame_count;
            frame_count = 0;
            fps_timer = cur_time;
        }

        // Memory load
        struct sysinfo_k si;
        memset(&si, 0, sizeof(si));
        sysinfo_k(&si);

        // Render real-time graphics HUD
        gfx_draw_hud("Vulkan 1.1 / KyroVK", fps, frame_time_ms, si.mem_used_kb, si.mem_total_kb);

        // Present frame via Vulkan Swapchain
        vkQueuePresentKHR(queue, &pres_info);

        rot_x += 1.8f;
        rot_y += 2.4f;
    }

    print("vkcube: Exited cleanly.\n");

    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    return 0;
}
