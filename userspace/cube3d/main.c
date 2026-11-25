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

// UI Button structure
typedef struct {
    int x, y, w, h;
    const char *label;
    uint32_t bg_normal;
    uint32_t bg_hover;
    bool is_hovered;
} ui_button_t;

static inline uint32_t shade_color(uint32_t color, float intensity) {
    if (intensity < 0.25f) intensity = 0.25f;
    if (intensity > 1.0f) intensity = 1.0f;
    uint32_t a = (color >> 24) & 0xFF;
    uint32_t r = (uint32_t)(((color >> 16) & 0xFF) * intensity);
    uint32_t g = (uint32_t)(((color >> 8) & 0xFF) * intensity);
    uint32_t b = (uint32_t)((color & 0xFF) * intensity);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static bool is_point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: cube3d (or cube)\n");
        print("Interactive 3D Vulkan Cube with Full Mouse Cursor & Drag Controls.\n");
        print("Controls:\n");
        print("  - Left Mouse Drag: Rotate cube freely\n");
        print("  - Right Mouse Drag: Zoom in / Zoom out\n");
        print("  - Space: Toggle auto-spin\n");
        print("  - 'm': Toggle solid / wireframe\n");
        print("  - 'r': Reset view\n");
        print("  - 'q', Esc, or Ctrl+C: Exit\n");
        print_flush();
        return 0;
    }

    if (gfx_init() != 0) {
        print("cube3d: Failed to initialize graphics mode.\n");
        return 1;
    }

    uint32_t screen_w = gfx_get_width();
    uint32_t screen_h = gfx_get_height();
    if (screen_w == 0) screen_w = 1024;
    if (screen_h == 0) screen_h = 768;

    int center_x = (int)(screen_w / 2);
    int center_y = (int)(screen_h / 2);
    float base_scale = (float)(screen_h / 2.8f);
    float current_scale = base_scale;

    float rot_x = 25.0f;
    float rot_y = 35.0f;
    float rot_z = 0.0f;

    bool auto_spin = false;
    bool solid_mode = true;
    bool running = true;

    // Mouse state
    int mouse_x = center_x;
    int mouse_y = center_y;
    int prev_mouse_x = mouse_x;
    int prev_mouse_y = mouse_y;
    bool left_button_down = false;
    bool right_button_down = false;

    // Initialize UI Buttons at the top
    ui_button_t btn_spin = {screen_w - 450, 8, 110, 26, "Auto-Spin", 0xFF2D3748, 0xFF4A5568, false};
    ui_button_t btn_mode = {screen_w - 330, 8, 110, 26, "Wireframe", 0xFF2D3748, 0xFF4A5568, false};
    ui_button_t btn_reset = {screen_w - 210, 8, 90, 26, "Reset", 0xFF2D3748, 0xFF4A5568, false};
    ui_button_t btn_exit = {screen_w - 110, 8, 85, 26, "Exit", 0xFF9B2C2C, 0xFFE53E3E, false};

    uint64_t last_time = get_ticks();
    uint32_t fps = 60;
    uint32_t frame_count = 0;
    uint64_t fps_timer = last_time;

    // Directional light vector
    const float lx = 0.57735f, ly = 0.57735f, lz = 0.57735f;

    // Drain initial events
    gfx_event_t drain_ev;
    while (gfx_poll_event(&drain_ev)) {}

    bool dirty = true;
    while (running) {
        // Poll input events
        gfx_event_t ev;
        memset(&ev, 0, sizeof(ev));
        while (gfx_poll_event(&ev)) {
            dirty = true;
            if (ev.type == GFX_EVENT_KEY_DOWN) {
                char c = (char)ev.data1;
                if (c == 'q' || c == 'Q' || c == 27 || ev.data1 == 27 || c == 3) {
                    running = false;
                    break;
                } else if (c == ' ') {
                    auto_spin = !auto_spin;
                } else if (c == 'm' || c == 'M') {
                    solid_mode = !solid_mode;
                } else if (c == 'r' || c == 'R') {
                    rot_x = 25.0f;
                    rot_y = 35.0f;
                    rot_z = 0.0f;
                    current_scale = base_scale;
                }
            } else if (ev.type == GFX_EVENT_MOUSE_MOVE) {
                mouse_x = ev.data1;
                mouse_y = ev.data2;
                if (left_button_down) {
                    float dx = (float)(mouse_x - prev_mouse_x);
                    float dy = (float)(mouse_y - prev_mouse_y);
                    rot_y += dx * 0.6f;
                    rot_x -= dy * 0.6f;
                } else if (right_button_down) {
                    float dy = (float)(mouse_y - prev_mouse_y);
                    current_scale -= dy * 1.5f;
                    if (current_scale < 100.0f) current_scale = 100.0f;
                    if (current_scale > 700.0f) current_scale = 700.0f;
                }
                prev_mouse_x = mouse_x;
                prev_mouse_y = mouse_y;
            } else if (ev.type == GFX_EVENT_MOUSE_DOWN) {
                mouse_x = ev.data2;
                mouse_y = ev.data3;
                prev_mouse_x = mouse_x;
                prev_mouse_y = mouse_y;
                if (ev.data1 == 1) { // Left click
                    left_button_down = true;
                    // Check button clicks
                    if (is_point_in_rect(mouse_x, mouse_y, btn_spin.x, btn_spin.y, btn_spin.w, btn_spin.h)) {
                        auto_spin = !auto_spin;
                    } else if (is_point_in_rect(mouse_x, mouse_y, btn_mode.x, btn_mode.y, btn_mode.w, btn_mode.h)) {
                        solid_mode = !solid_mode;
                    } else if (is_point_in_rect(mouse_x, mouse_y, btn_reset.x, btn_reset.y, btn_reset.w, btn_reset.h)) {
                        rot_x = 25.0f; rot_y = 35.0f; rot_z = 0.0f; current_scale = base_scale;
                    } else if (is_point_in_rect(mouse_x, mouse_y, btn_exit.x, btn_exit.y, btn_exit.w, btn_exit.h)) {
                        running = false;
                        break;
                    }
                } else if (ev.data1 == 2) { // Right click
                    right_button_down = true;
                }
            } else if (ev.type == GFX_EVENT_MOUSE_UP) {
                mouse_x = ev.data2;
                mouse_y = ev.data3;
                prev_mouse_x = mouse_x;
                prev_mouse_y = mouse_y;
                if (ev.data1 == 1) left_button_down = false;
                if (ev.data1 == 2) right_button_down = false;
            }
        }
        if (!running) break;

        if (!dirty && !auto_spin) {
            sleep_ms(10);
            continue;
        }
        dirty = false;

        // Auto-spin if enabled
        if (auto_spin) {
            rot_y += 1.5f;
            rot_x += 0.8f;
        }

        // Clamp rotation angles
        if (rot_x >= 360.0f) rot_x -= 360.0f; else if (rot_x < 0.0f) rot_x += 360.0f;
        if (rot_y >= 360.0f) rot_y -= 360.0f; else if (rot_y < 0.0f) rot_y += 360.0f;

        // Clear screen (deep charcoal) and depth buffer
        gfx_clear(0xFF141923);
        vkClearDepthBuffer();

        // Header bar
        gfx_draw_rect(0, 0, screen_w, 42, 0xFF1E2633);
        gfx_draw_rect(0, 41, screen_w, 2, 0xFF3182CE); // Sky Blue accent line
        gfx_draw_string(24, 13, "KyroOS 3D Interactive Cube (Drag with Mouse!)", 0xFFFFFFFF, 0);

        // Update Button hover states
        btn_spin.is_hovered = is_point_in_rect(mouse_x, mouse_y, btn_spin.x, btn_spin.y, btn_spin.w, btn_spin.h);
        btn_mode.is_hovered = is_point_in_rect(mouse_x, mouse_y, btn_mode.x, btn_mode.y, btn_mode.w, btn_mode.h);
        btn_reset.is_hovered = is_point_in_rect(mouse_x, mouse_y, btn_reset.x, btn_reset.y, btn_reset.w, btn_reset.h);
        btn_exit.is_hovered = is_point_in_rect(mouse_x, mouse_y, btn_exit.x, btn_exit.y, btn_exit.w, btn_exit.h);

        // Draw Buttons
        ui_button_t *btns[] = {&btn_spin, &btn_mode, &btn_reset, &btn_exit};
        for (int b = 0; b < 4; b++) {
            ui_button_t *btn = btns[b];
            uint32_t bg = btn->is_hovered ? btn->bg_hover : btn->bg_normal;
            gfx_draw_rect(btn->x, btn->y, btn->w, btn->h, bg);
            gfx_draw_rect(btn->x, btn->y, btn->w, 1, 0xFF718096);
            gfx_draw_rect(btn->x, btn->y + btn->h - 1, btn->w, 1, 0xFF1A202C);

            const char *label = btn->label;
            if (btn == &btn_spin) label = auto_spin ? "Spin: ON" : "Spin: OFF";
            if (btn == &btn_mode) label = solid_mode ? "Solid" : "Wire";

            int text_len = strlen(label);
            int tx = btn->x + (btn->w - text_len * 8) / 2;
            int ty = btn->y + (btn->h - 16) / 2;
            gfx_draw_string(tx, ty, label, 0xFFFFFFFF, 0);
        }

        // Trigonometry for rotation
        float rad_x = rot_x * (float)(PI / 180.0);
        float rad_y = rot_y * (float)(PI / 180.0);
        float rad_z = rot_z * (float)(PI / 180.0);

        float cx = cosf(rad_x), sx = sinf(rad_x);
        float cy = cosf(rad_y), sy = sinf(rad_y);
        float cz = cosf(rad_z), sz = sinf(rad_z);

        // Project 8 vertices
        proj_point_t proj[8];
        for (int i = 0; i < 8; i++) {
            float vx = cube_vertices[i].x;
            float vy = cube_vertices[i].y;
            float vz = cube_vertices[i].z;

            // Z
            float x1 = vx * cz - vy * sz;
            float y1 = vx * sz + vy * cz;
            float z1 = vz;

            // Y
            float x2 = x1 * cy + z1 * sy;
            float y2 = y1;
            float z2 = -x1 * sy + z1 * cy;

            // X
            float x3 = x2;
            float y3 = y2 * cx - z2 * sx;
            float z3 = y2 * sx + z2 * cx;

            float z_proj = z3 + 3.2f;
            if (z_proj < 0.2f) z_proj = 0.2f;

            proj[i].x = (float)center_x + (x3 * current_scale) / z_proj;
            proj[i].y = (float)center_y + (y3 * current_scale) / z_proj;
            proj[i].z = z_proj;
        }

        // Render Solid 3D Shaded Triangles
        if (solid_mode) {
            for (int t = 0; t < 12; t++) {
                const cube_triangle_t *tri = &cube_triangles[t];
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

                float dot = rnx * lx + rny * ly + rnz * lz;
                float intensity = 0.35f + 0.65f * (dot > 0.0f ? dot : 0.0f);
                uint32_t final_color = shade_color(tri->base_color, intensity);

                VkVertex3D v0 = {proj[tri->v0].x, proj[tri->v0].y, proj[tri->v0].z, final_color};
                VkVertex3D v1 = {proj[tri->v1].x, proj[tri->v1].y, proj[tri->v1].z, final_color};
                VkVertex3D v2 = {proj[tri->v2].x, proj[tri->v2].y, proj[tri->v2].z, final_color};

                vkRasterizeTriangle(&v0, &v1, &v2);
            }
        }

        // Overlay Wireframe Edges
        for (int e = 0; e < 12; e++) {
            int p0 = cube_edges[e][0];
            int p1 = cube_edges[e][1];
            uint32_t edge_col = solid_mode ? 0xFFFFFFFF : 0xFF38B2AC;
            gfx_draw_line((int)proj[p0].x, (int)proj[p0].y, (int)proj[p1].x, (int)proj[p1].y, edge_col);
        }

        // FPS Calculation
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

        // Interactive Info HUD (Bottom Left)
        int hud_x = 24;
        int hud_y = screen_h - 130;
        gfx_draw_rect(hud_x, hud_y, 320, 105, 0xDD1A202C);
        gfx_draw_rect(hud_x, hud_y, 320, 2, 0xFF4A5568);

        char hud_buf[64];
        snprintf(hud_buf, sizeof(hud_buf), "FPS: %d (%d ms) | Interactive 3D", fps, frame_time_ms);
        gfx_draw_string(hud_x + 12, hud_y + 12, hud_buf, 0xFF48BB78, 0);

        snprintf(hud_buf, sizeof(hud_buf), "Rotation: Pitch %d deg, Yaw %d deg", (int)rot_x, (int)rot_y);
        gfx_draw_string(hud_x + 12, hud_y + 34, hud_buf, 0xFFE2E8F0, 0);

        snprintf(hud_buf, sizeof(hud_buf), "Mouse: (%d, %d) [%s%s]",
                 mouse_x, mouse_y,
                 left_button_down ? "Drag" : "Idle",
                 right_button_down ? " Zoom" : "");
        gfx_draw_string(hud_x + 12, hud_y + 56, hud_buf, 0xFFA0AEC0, 0);

        gfx_draw_string(hud_x + 12, hud_y + 78, "Controls: Left-Drag=Rotate, Right-Drag=Zoom", 0xFF63B3ED, 0);

        // Always render visible mouse cursor on top!
        gfx_draw_cursor(mouse_x, mouse_y);

        // Present double buffer
        gfx_flip();
    }

    gfx_clear(0x00000000);
    gfx_flip();
    print("\033[2J\033[H");
    print("cube3d exited cleanly.\n");
    return 0;
}
