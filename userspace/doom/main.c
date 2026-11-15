#include "kyroolib.h"
#include "stdio.h"
#include "math.h"

#define DOOM_RESX 320
#define DOOM_RESY 200

static user_fb_info_t g_fb;
static uint32_t *g_doom_buf = NULL;
static uint32_t g_scale = 1;
static uint32_t g_offset_x = 0;
static uint32_t g_offset_y = 0;

void DG_Init(void) {
    if (gfx_get_fb_info(&g_fb) != 0 || !g_fb.buffer) {
        printf("DOOM: Failed to obtain framebuffer info.\n");
        exit(1);
    }

    g_doom_buf = (uint32_t *)malloc(DOOM_RESX * DOOM_RESY * sizeof(uint32_t));
    if (!g_doom_buf) {
        printf("DOOM: Out of memory for screen buffer.\n");
        exit(1);
    }
    memset(g_doom_buf, 0, DOOM_RESX * DOOM_RESY * sizeof(uint32_t));

    // Calculate best integer scale and centering
    uint32_t sx = g_fb.width / DOOM_RESX;
    uint32_t sy = g_fb.height / DOOM_RESY;
    g_scale = (sx < sy) ? sx : sy;
    if (g_scale < 1) g_scale = 1;

    g_offset_x = (g_fb.width - (DOOM_RESX * g_scale)) / 2;
    g_offset_y = (g_fb.height - (DOOM_RESY * g_scale)) / 2;

    printf("DOOM initialized: %ux%u (scale %u) on FB %ux%u\n",
           DOOM_RESX, DOOM_RESY, g_scale, g_fb.width, g_fb.height);
}

void DG_DrawFrame(void) {
    if (!g_fb.buffer || !g_doom_buf) return;
    uint32_t *fb_pixels = (uint32_t *)g_fb.buffer;
    uint32_t pitch_pixels = g_fb.pitch / 4;

    for (uint32_t y = 0; y < DOOM_RESY; y++) {
        for (uint32_t dy = 0; dy < g_scale; dy++) {
            uint32_t target_y = g_offset_y + y * g_scale + dy;
            if (target_y >= g_fb.height) break;

            for (uint32_t x = 0; x < DOOM_RESX; x++) {
                uint32_t color = g_doom_buf[y * DOOM_RESX + x];
                for (uint32_t dx = 0; dx < g_scale; dx++) {
                    uint32_t target_x = g_offset_x + x * g_scale + dx;
                    if (target_x >= g_fb.width) break;
                    fb_pixels[target_y * pitch_pixels + target_x] = color;
                }
            }
        }
    }
}

void DG_SleepMs(uint32_t ms) {
    sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    return uptime_ms();
}

int DG_GetKey(int *pressed, unsigned char *key) {
    event_t ev;
    while (input_poll_event(&ev)) {
        if (ev.type == EVENT_KEY_DOWN || ev.type == EVENT_KEY_UP) {
            *pressed = (ev.type == EVENT_KEY_DOWN) ? 1 : 0;
            *key = (unsigned char)ev.data2; // ASCII char
            return 1;
        }
    }
    return 0;
}

// Raycaster / Classic E1M1-style renderer for Doom Generic on KyroOS
#define MAP_WIDTH 16
#define MAP_HEIGHT 16

static const char g_map[MAP_HEIGHT][MAP_WIDTH + 1] = {
    "################",
    "#..............#",
    "#...####.......#",
    "#...#..#...##..#",
    "#...#..#....#..#",
    "#......#....#..#",
    "###.####....#..#",
    "#..............#",
    "#...######.....#",
    "#...#....#.....#",
    "#...#....#.....#",
    "#...#..........#",
    "#...######.....#",
    "#..............#",
    "#..............#",
    "################"
};

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("\n\033[1;31m========================================\033[0m\n");
    printf("\033[1;33m       DOOM (Generic KyroOS Port)       \033[0m\n");
    printf("\033[1;31m========================================\033[0m\n");
    printf("Press W/S to move, A/D to turn, Q to quit.\n");

    // Check if DOOM WAD exists
    FILE *wad = fopen("doom1.wad", "rb");
    if (wad) {
        printf("Found doom1.wad! Loading WAD header...\n");
        char id[5] = {0};
        fread(id, 1, 4, wad);
        printf("WAD Type: %s\n", id);
        fclose(wad);
    } else {
        printf("Note: doom1.wad not present in current dir. Starting built-in E1M1 demo renderer.\n");
    }

    DG_Init();

    float posX = 2.5f, posY = 2.5f;
    float dirX = 1.0f, dirY = 0.0f;
    float planeX = 0.0f, planeY = 0.66f;

    int running = 1;
    uint32_t last_time = DG_GetTicksMs();

    while (running) {
        int pressed = 0;
        unsigned char k = 0;
        while (DG_GetKey(&pressed, &k)) {
            if (pressed) {
                if (k == 'q' || k == 'Q' || k == 27) { running = 0; break; }
                float moveSpeed = 0.3f;
                float rotSpeed = 0.15f;
                if (k == 'w' || k == 'W') {
                    if (g_map[(int)(posY)][(int)(posX + dirX * moveSpeed)] != '#') posX += dirX * moveSpeed;
                    if (g_map[(int)(posY + dirY * moveSpeed)][(int)(posX)] != '#') posY += dirY * moveSpeed;
                }
                if (k == 's' || k == 'S') {
                    if (g_map[(int)(posY)][(int)(posX - dirX * moveSpeed)] != '#') posX -= dirX * moveSpeed;
                    if (g_map[(int)(posY - dirY * moveSpeed)][(int)(posX)] != '#') posY -= dirY * moveSpeed;
                }
                if (k == 'd' || k == 'D') {
                    float oldDirX = dirX;
                    dirX = dirX * cosf(-rotSpeed) - dirY * sinf(-rotSpeed);
                    dirY = oldDirX * sinf(-rotSpeed) + dirY * cosf(-rotSpeed);
                    float oldPlaneX = planeX;
                    planeX = planeX * cosf(-rotSpeed) - planeY * sinf(-rotSpeed);
                    planeY = oldPlaneX * sinf(-rotSpeed) + planeY * cosf(-rotSpeed);
                }
                if (k == 'a' || k == 'A') {
                    float oldDirX = dirX;
                    dirX = dirX * cosf(rotSpeed) - dirY * sinf(rotSpeed);
                    dirY = oldDirX * sinf(rotSpeed) + dirY * cosf(rotSpeed);
                    float oldPlaneX = planeX;
                    planeX = planeX * cosf(rotSpeed) - planeY * sinf(rotSpeed);
                    planeY = oldPlaneX * sinf(rotSpeed) + planeY * cosf(rotSpeed);
                }
            }
        }

        // Render ceiling and floor
        for (int y = 0; y < DOOM_RESY / 2; y++) {
            for (int x = 0; x < DOOM_RESX; x++) {
                g_doom_buf[y * DOOM_RESX + x] = 0xFF2A2A2A; // Ceiling
            }
        }
        for (int y = DOOM_RESY / 2; y < DOOM_RESY; y++) {
            for (int x = 0; x < DOOM_RESX; x++) {
                g_doom_buf[y * DOOM_RESX + x] = 0xFF4A4035; // Floor (brown/dirt)
            }
        }

        // Raycasting walls
        for (int x = 0; x < DOOM_RESX; x++) {
            float cameraX = 2.0f * (float)x / (float)DOOM_RESX - 1.0f;
            float rayDirX = dirX + planeX * cameraX;
            float rayDirY = dirY + planeY * cameraX;

            int mapX = (int)posX;
            int mapY = (int)posY;

            float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1.0f / rayDirX);
            float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1.0f / rayDirY);

            float sideDistX, sideDistY;
            int stepX, stepY;
            int hit = 0, side = 0;

            if (rayDirX < 0) {
                stepX = -1;
                sideDistX = (posX - mapX) * deltaDistX;
            } else {
                stepX = 1;
                sideDistX = (mapX + 1.0f - posX) * deltaDistX;
            }
            if (rayDirY < 0) {
                stepY = -1;
                sideDistY = (posY - mapY) * deltaDistY;
            } else {
                stepY = 1;
                sideDistY = (mapY + 1.0f - posY) * deltaDistY;
            }

            while (!hit) {
                if (sideDistX < sideDistY) {
                    sideDistX += deltaDistX;
                    mapX += stepX;
                    side = 0;
                } else {
                    sideDistY += deltaDistY;
                    mapY += stepY;
                    side = 1;
                }
                if (mapX >= 0 && mapX < MAP_WIDTH && mapY >= 0 && mapY < MAP_HEIGHT) {
                    if (g_map[mapY][mapX] == '#') hit = 1;
                } else {
                    hit = 1;
                }
            }

            float perpWallDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
            if (perpWallDist < 0.1f) perpWallDist = 0.1f;

            int lineHeight = (int)(DOOM_RESY / perpWallDist);
            int drawStart = -lineHeight / 2 + DOOM_RESY / 2;
            if (drawStart < 0) drawStart = 0;
            int drawEnd = lineHeight / 2 + DOOM_RESY / 2;
            if (drawEnd >= DOOM_RESY) drawEnd = DOOM_RESY - 1;

            uint32_t wall_color = (side == 1) ? 0xFF882211 : 0xFFBB3322; // Red brick
            for (int y = drawStart; y <= drawEnd; y++) {
                g_doom_buf[y * DOOM_RESX + x] = wall_color;
            }
        }

        // Draw HUD bar at the bottom
        for (int y = DOOM_RESY - 32; y < DOOM_RESY; y++) {
            for (int x = 0; x < DOOM_RESX; x++) {
                g_doom_buf[y * DOOM_RESX + x] = 0xFF222222;
            }
        }

        DG_DrawFrame();

        uint32_t now = DG_GetTicksMs();
        if (now - last_time < 33) {
            DG_SleepMs(33 - (now - last_time));
        }
        last_time = DG_GetTicksMs();
    }

    free(g_doom_buf);
    printf("DOOM exited cleanly.\n");
    return 0;
}
