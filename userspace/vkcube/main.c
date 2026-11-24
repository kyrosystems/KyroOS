#include <vulkan/vulkan.h>
#include <kyroolib.h>
#include <kyroos_gfx.h>
#include <math.h>
#include <stdbool.h>

typedef struct {
    float x, y, z;
} vec3_t;

typedef struct {
    float x, y, z;
} proj_point_t;

// 8 Cube vertices (-1.0 to +1.0)
static const vec3_t cube_vertices[8] = {
    {-1.0f, -1.0f, -1.0f}, // 0: left-bottom-back
    { 1.0f, -1.0f, -1.0f}, // 1: right-bottom-back
    { 1.0f,  1.0f, -1.0f}, // 2: right-top-back
    {-1.0f,  1.0f, -1.0f}, // 3: left-top-back
    {-1.0f, -1.0f,  1.0f}, // 4: left-bottom-front
    { 1.0f, -1.0f,  1.0f}, // 5: right-bottom-front
    { 1.0f,  1.0f,  1.0f}, // 6: right-top-front
    {-1.0f,  1.0f,  1.0f}  // 7: left-top-front
};

// 12 Triangles (2 per face, CCW order)
typedef struct {
    int v0, v1, v2;
    vec3_t normal;
    uint32_t base_color;
} cube_triangle_t;

static const cube_triangle_t cube_triangles[12] = {
    // Front face (Z = +1, Red)
    {4, 5, 6, { 0.0f,  0.0f,  1.0f}, 0xFFFF3333},
    {4, 6, 7, { 0.0f,  0.0f,  1.0f}, 0xFFFF3333},
    // Back face (Z = -1, Cyan)
    {1, 0, 3, { 0.0f,  0.0f, -1.0f}, 0xFF33FFFF},
    {1, 3, 2, { 0.0f,  0.0f, -1.0f}, 0xFF33FFFF},
    // Top face (Y = +1, Green)
    {7, 6, 2, { 0.0f,  1.0f,  0.0f}, 0xFF33FF33},
    {7, 2, 3, { 0.0f,  1.0f,  0.0f}, 0xFF33FF33},
    // Bottom face (Y = -1, Magenta)
    {0, 1, 5, { 0.0f, -1.0f,  0.0f}, 0xFFFF33FF},
    {0, 5, 4, { 0.0f, -1.0f,  0.0f}, 0xFFFF33FF},
    // Left face (X = -1, Blue)
    {0, 4, 7, {-1.0f,  0.0f,  0.0f}, 0xFF3366FF},
    {0, 7, 3, {-1.0f,  0.0f,  0.0f}, 0xFF3366FF},
    // Right face (X = +1, Yellow)
    {5, 1, 2, { 1.0f,  0.0f,  0.0f}, 0xFFFFFF33},
    {5, 2, 6, { 1.0f,  0.0f,  0.0f}, 0xFFFFFF33}
};

// 12 Wireframe Edges
static const int cube_edges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Back
    {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Front
    {0, 4}, {1, 5}, {2, 6}, {3, 7}  // Sides
};

static inline uint32_t shade_color(uint32_t color, float intensity) {
    if (intensity < 0.25f) intensity = 0.25f;
    if (intensity > 1.0f) intensity = 1.0f;
    uint32_t a = (color >> 24) & 0xFF;
    uint32_t r = (uint32_t)(((color >> 16) & 0xFF) * intensity);
    uint32_t g = (uint32_t)(((color >> 8) & 0xFF) * intensity);
    uint32_t b = (uint32_t)((color & 0xFF) * intensity);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: vkcube\n");
        print("KyroOS Vulkan 1.1 3D Spinning Cube Demo with Solid Shading and Depth Buffer.\n");
        print("Controls: 'q' or Esc to exit, Space to toggle wireframe/solid.\n");
        print_flush();
        return 0;
    }

    print("Initializing Vulkan 1.1 on KyroOS...\n");

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
    float scale = (float)(screen_h / 2.8f);

    VkPresentInfoKHR pres_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .swapchainCount = 1,
        .pSwapchains = &swapchain
    };

    float rot_x = 25.0f;
    float rot_y = 35.0f;
    float rot_z = 15.0f;
    bool running = true;
    bool solid_mode = true;

    uint64_t last_time = get_ticks();
    uint32_t fps = 60;
    uint32_t frame_count = 0;
    uint64_t fps_timer = last_time;

    // Directional light vector (normalized: 0.577, 0.577, 0.577)
    const float lx = 0.57735f, ly = 0.57735f, lz = 0.57735f;

    gfx_event_t drain_ev;
    while (gfx_poll_event(&drain_ev)) {}

    // Render loop
    while (running) {
        // Poll input events
        gfx_event_t ev;
        memset(&ev, 0, sizeof(ev));
        while (gfx_poll_event(&ev)) {
            if (ev.type == GFX_EVENT_KEY_DOWN) {
                char c = (char)ev.data1;
                if (c == 'q' || c == 'Q' || c == 27 || ev.data1 == 27 || c == 3) {
                    running = false;
                    break;
                } else if (c == ' ') {
                    solid_mode = !solid_mode;
                }
            }
        }
        if (!running) break;

        // Clear Framebuffer (Deep charcoal slate) & Depth Buffer
        gfx_clear(0xFF12161E);
        vkClearDepthBuffer();

        // Top Vulkan Header
        gfx_draw_rect(0, 0, screen_w, 42, 0xFF1A202C);
        gfx_draw_rect(0, 41, screen_w, 2, 0xFFE03131); // Vulkan Red accent line
        gfx_draw_string(24, 14, "KyroOS Vulkan 1.1 - 3D Spinning Cube", 0xFFFFFFFF, 0);
        gfx_draw_string(screen_w - 300, 14, "[ Q/Esc: Exit | Space: Toggle ]", 0xFFA0AEC0, 0);

        // Rotation angles
        rot_x += 1.2f; if (rot_x >= 360.0f) rot_x -= 360.0f;
        rot_y += 1.8f; if (rot_y >= 360.0f) rot_y -= 360.0f;
        rot_z += 0.8f; if (rot_z >= 360.0f) rot_z -= 360.0f;

        float rad_x = rot_x * (float)(PI / 180.0);
        float rad_y = rot_y * (float)(PI / 180.0);
        float rad_z = rot_z * (float)(PI / 180.0);

        float cx = cosf(rad_x), sx = sinf(rad_x);
        float cy = cosf(rad_y), sy = sinf(rad_y);
        float cz = cosf(rad_z), sz = sinf(rad_z);

        // Transform all 8 vertices
        proj_point_t proj[8];
        for (int i = 0; i < 8; i++) {
            float vx = cube_vertices[i].x;
            float vy = cube_vertices[i].y;
            float vz = cube_vertices[i].z;

            // Rotate around Z
            float x1 = vx * cz - vy * sz;
            float y1 = vx * sz + vy * cz;
            float z1 = vz;

            // Rotate around Y
            float x2 = x1 * cy + z1 * sy;
            float y2 = y1;
            float z2 = -x1 * sy + z1 * cy;

            // Rotate around X
            float x3 = x2;
            float y3 = y2 * cx - z2 * sx;
            float z3 = y2 * sx + z2 * cx;

            // Perspective projection
            float z_proj = z3 + 3.2f;
            if (z_proj < 0.2f) z_proj = 0.2f;

            proj[i].x = (float)center_x + (x3 * scale) / z_proj;
            proj[i].y = (float)center_y + (y3 * scale) / z_proj;
            proj[i].z = z_proj;
        }

        // Draw Solid Shaded Faces with Vulkan Software Rasterizer & Depth Buffer
        if (solid_mode) {
            for (int t = 0; t < 12; t++) {
                const cube_triangle_t *tri = &cube_triangles[t];
                // Rotate normal
                float nx = tri->normal.x, ny = tri->normal.y, nz = tri->normal.z;
                float nx1 = nx * cz - ny * sz;
                float ny1 = nx * sz + ny * cz;
                float nz1 = nz;
                float nx2 = nx1 * cy + nz1 * sy;
                float ny2 = ny1;
                float nz2 = -nx1 * sy + nz1 * cy;
                float rnx = nx2;
                float rny = ny2 * cx - nz2 * sx;
                float rnz = ny2 * sx + nz2 * cx;

                // Directional diffuse lighting
                float dot = rnx * lx + rny * ly + rnz * lz;
                float intensity = 0.35f + 0.65f * (dot > 0.0f ? dot : 0.0f);
                uint32_t final_color = shade_color(tri->base_color, intensity);

                VkVertex3D v0 = {proj[tri->v0].x, proj[tri->v0].y, proj[tri->v0].z, final_color};
                VkVertex3D v1 = {proj[tri->v1].x, proj[tri->v1].y, proj[tri->v1].z, final_color};
                VkVertex3D v2 = {proj[tri->v2].x, proj[tri->v2].y, proj[tri->v2].z, final_color};

                vkRasterizeTriangle(&v0, &v1, &v2);
            }
        }

        // Draw crisp overlay edges
        for (int e = 0; e < 12; e++) {
            int p0 = cube_edges[e][0];
            int p1 = cube_edges[e][1];
            uint32_t edge_col = solid_mode ? 0xFFFFFFFF : 0xFF33CCFF;
            gfx_draw_line((int)proj[p0].x, (int)proj[p0].y, (int)proj[p1].x, (int)proj[p1].y, edge_col);
        }

        // FPS & Performance HUD
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

        // HUD Card (Bottom Left)
        int hud_x = 24;
        int hud_y = screen_h - 130;
        gfx_draw_rect(hud_x, hud_y, 300, 105, 0xDD1A202C);
        gfx_draw_rect(hud_x, hud_y, 300, 2, 0xFF4A5568);

        char hud_buf[64];
        snprintf(hud_buf, sizeof(hud_buf), "FPS: %d (%d ms/frame)", fps, frame_time_ms);
        gfx_draw_string(hud_x + 12, hud_y + 12, hud_buf, 0xFF48BB78, 0); // Green

        gfx_draw_string(hud_x + 12, hud_y + 34, "Driver: KyroVK Software 1.1", 0xFFE2E8F0, 0);

        snprintf(hud_buf, sizeof(hud_buf), "Pipeline: %s (Depth: Z-Buffer 32b)", solid_mode ? "Solid Shaded" : "Wireframe");
        gfx_draw_string(hud_x + 12, hud_y + 56, hud_buf, 0xFFA0AEC0, 0);

        gfx_draw_string(hud_x + 12, hud_y + 78, "Geometry: 12 Triangles, 8 Verts", 0xFFA0AEC0, 0);

        // Present Framebuffer via Vulkan Swapchain
        vkQueuePresentKHR(queue, &pres_info);
    }

    // Cleanup Vulkan resources
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    print("Vulkan demo closed successfully.\n");
    return 0;
}
