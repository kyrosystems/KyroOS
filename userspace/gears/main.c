#include <GL/gl.h>
#include <GL/glu.h>
#include <kyroolib.h>
#include <kyroos_gfx.h>
#include <stdbool.h>

static void draw_cube(void) {
    glBegin(GL_QUADS);

    // Front Face (Red)
    glColor3f(1.0f, 0.2f, 0.2f);
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);
    glVertex3f( 1.0f,  1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);

    // Back Face (Green)
    glColor3f(0.2f, 1.0f, 0.2f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);
    glVertex3f( 1.0f,  1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);

    // Top Face (Blue)
    glColor3f(0.2f, 0.4f, 1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);
    glVertex3f( 1.0f,  1.0f,  1.0f);
    glVertex3f( 1.0f,  1.0f, -1.0f);

    // Bottom Face (Yellow)
    glColor3f(1.0f, 1.0f, 0.2f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);

    // Right Face (Magenta)
    glColor3f(1.0f, 0.2f, 1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f,  1.0f, -1.0f);
    glVertex3f( 1.0f,  1.0f,  1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);

    // Left Face (Cyan)
    glColor3f(0.2f, 1.0f, 1.0f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);

    glEnd();
}

static void draw_pyramid(void) {
    glBegin(GL_TRIANGLES);

    // Front
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f( 0.0f,  1.0f,  0.0f);
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);

    // Right
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f( 0.0f,  1.0f,  0.0f);
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);

    // Back
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f( 0.0f,  1.0f,  0.0f);
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f(-1.0f, -1.0f, -1.0f);

    // Left
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f( 0.0f,  1.0f,  0.0f);
    glColor3f(0.0f, 0.0f, 1.0f);
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);

    glEnd();
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print("Usage: gears\n");
        print("KyroOS OpenGL Mesa 3D Spinning Geometry Demo with Real-time Performance HUD.\n");
        print("Controls: 'q' or Esc to exit.\n");
        print_flush();
        return 0;
    }

    print("Starting Mesa OpenGL Demo (Press 'q' or Esc to exit)...\n");

    if (glInitKyroOS() != 0) {
        print("gears: Failed to initialize OpenGL on KyroOS framebuffer.\n");
        return 1;
    }

    uint32_t width = gfx_get_width();
    uint32_t height = gfx_get_height();

    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, (double)width / (double)height, 0.1, 100.0);

    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);

    float angle_x = 0.0f;
    float angle_y = 0.0f;
    bool running = true;

    uint64_t last_time = get_ticks();
    uint32_t fps = 60;
    uint32_t frame_count = 0;
    uint64_t fps_timer = last_time;

    gfx_event_t drain_ev;
    while (gfx_poll_event(&drain_ev)) {}

    while (running) {
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

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Header banner
        gfx_draw_rect(0, 0, width, 40, 0xFF1C2430);
        gfx_draw_rect(0, 39, width, 2, 0xFF3080E0); // OpenGL Blue accent line
        gfx_draw_string(20, 12, "KyroOS Mesa OpenGL 3D Demo", 0xFFFFFFFF, 0);
        gfx_draw_string(width - 220, 12, "[ Press 'q' or Esc to exit ]", 0xFFAAAAAA, 0);

        // Render Cube on Left
        glLoadIdentity();
        glTranslatef(-1.8f, 0.0f, -6.0f);
        glRotatef(angle_x, 1.0f, 0.0f, 0.0f);
        glRotatef(angle_y, 0.0f, 1.0f, 0.0f);
        draw_cube();

        // Render Pyramid on Right
        glLoadIdentity();
        glTranslatef(1.8f, 0.0f, -6.0f);
        glRotatef(angle_y, 0.0f, 1.0f, 0.0f);
        glRotatef(angle_x, 1.0f, 1.0f, 0.0f);
        draw_pyramid();

        // Timing & FPS
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

        struct sysinfo_k si;
        memset(&si, 0, sizeof(si));
        sysinfo_k(&si);

        gfx_draw_hud("OpenGL Mesa Softpipe", fps, frame_time_ms, si.mem_used_kb, si.mem_total_kb);

        glSwapBuffers();

        angle_x += 2.0f;
        angle_y += 3.0f;
    }

    return 0;
}
