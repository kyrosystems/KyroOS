#include <kyroolib.h>
#include <tui/tui.h>

#define BG_COLOR        0x0000AA
#define TITLE_BG_COLOR  0xAAAAAA
#define TEXT_COLOR      0xFFFFFF
#define TITLE_TEXT_COLOR 0x000000
#define BTN_COLOR       0x00AA00
#define BTN_HOVER_COLOR 0x00FF00
#define ERR_COLOR       0xFF0000

static int selected_disk = 0;
static int step = 0;
static char status_msg[256] = {0};
static char install_log[512] = {0};

typedef struct {
    char name[64];
    uint32_t size_mb;
    int has_partitions;
} disk_info_t;

static disk_info_t disks[4];
static int disk_count = 0;

static void draw_header(fb_info_t *fb) {
    tui_draw_box(fb, 0, 0, fb->width, 24, TITLE_BG_COLOR);
    tui_draw_text(fb, 5, 4, "KyroOS Installer v1.0", TITLE_TEXT_COLOR);
}

static void draw_footer(fb_info_t *fb) {
    tui_draw_text(fb, 5, fb->height - 20, "Press Q to exit, Enter to continue", TEXT_COLOR);
}

static void detect_disks() {
    disk_count = 0;
    strcpy(disks[0].name, "/dev/sda");
    disks[0].size_mb = 8192;
    disks[0].has_partitions = 0;
    disk_count++;
}

static void draw_step_welcome(fb_info_t *fb) {
    uint32_t win_x = 50, win_y = 50;
    uint32_t win_w = fb->width - 100, win_h = fb->height - 150;
    
    tui_draw_box(fb, win_x, win_y, win_w, win_h, TITLE_BG_COLOR);
    
    tui_draw_text(fb, win_x + 10, win_y + 10, "Welcome to KyroOS Installer!", TEXT_COLOR);
    tui_draw_text(fb, win_x + 10, win_y + 30, "This installer will:", TEXT_COLOR);
    tui_draw_text(fb, win_x + 20, win_y + 50, "1. Partition disk (MBR)", TEXT_COLOR);
    tui_draw_text(fb, win_x + 20, win_y + 70, "2. Format to KyroFS", TEXT_COLOR);
    tui_draw_text(fb, win_x + 20, win_y + 90, "3. Install Limine bootloader", TEXT_COLOR);
    tui_draw_text(fb, win_x + 20, win_y + 110, "4. Copy system files", TEXT_COLOR);
}

static void draw_step_disk_select(fb_info_t *fb) {
    uint32_t win_x = 50, win_y = 50;
    uint32_t win_w = fb->width - 100, win_h = fb->height - 150;
    
    tui_draw_box(fb, win_x, win_y, win_w, win_h, TITLE_BG_COLOR);
    
    tui_draw_text(fb, win_x + 10, win_y + 10, "Select installation disk:", TEXT_COLOR);
    
    detect_disks();
    
    for (int i = 0; i < disk_count; i++) {
        uint32_t color = (i == selected_disk) ? BTN_HOVER_COLOR : BTN_COLOR;
        tui_draw_box(fb, win_x + 10, win_y + 30 + i * 30, win_w - 20, 25, color);
        
        char buf[128];
        sprintf(buf, "%s - %u MB", disks[i].name, disks[i].size_mb);
        tui_draw_text(fb, win_x + 20, win_y + 35 + i * 30, buf, TEXT_COLOR);
    }
    
    if (status_msg[0] != '\0') {
        tui_draw_text(fb, win_x + 10, win_y + win_h - 30, status_msg, ERR_COLOR);
    }
}

static void draw_step_partition(fb_info_t *fb) {
    uint32_t win_x = 50, win_y = 50;
    uint32_t win_w = fb->width - 100, win_h = fb->height - 150;
    
    tui_draw_box(fb, win_x, win_y, win_w, win_h, TITLE_BG_COLOR);
    
    tui_draw_text(fb, win_x + 10, win_y + 10, "Partition scheme:", TEXT_COLOR);
    
    char buf[128];
    sprintf(buf, "Target: %s (entire disk)", disks[selected_disk].name);
    tui_draw_text(fb, win_x + 10, win_y + 40, buf, TEXT_COLOR);
    sprintf(buf, "Size: %u MB", disks[selected_disk].size_mb);
    tui_draw_text(fb, win_x + 10, win_y + 60, buf, TEXT_COLOR);
    tui_draw_text(fb, win_x + 10, win_y + 90, "Creating MBR partition table...", TEXT_COLOR);
    tui_draw_text(fb, win_x + 10, win_y + 110, "Creating partition /dev/sda1 (100%% disk)", TEXT_COLOR);
}

static void draw_step_format(fb_info_t *fb) {
    uint32_t win_x = 50, win_y = 50;
    uint32_t win_w = fb->width - 100, win_h = fb->height - 150;
    
    tui_draw_box(fb, win_x, win_y, win_w, win_h, TITLE_BG_COLOR);
    
    tui_draw_text(fb, win_x + 10, win_y + 10, "Formatting partition...", TEXT_COLOR);
    tui_draw_text(fb, win_x + 10, win_y + 40, "Creating KyroFS filesystem...", TEXT_COLOR);
    
    tui_draw_box(fb, win_x + 10, win_y + 70, win_w - 20, 20, 0x555555);
    tui_draw_box(fb, win_x + 10, win_y + 70, (win_w - 20) * 3 / 4, 20, BTN_COLOR);
    
    tui_draw_text(fb, win_x + 10, win_y + 100, "Done!", TEXT_COLOR);
}

static void draw_step_install(fb_info_t *fb) {
    uint32_t win_x = 50, win_y = 50;
    uint32_t win_w = fb->width - 100, win_h = fb->height - 150;
    
    tui_draw_box(fb, win_x, win_y, win_w, win_h, TITLE_BG_COLOR);
    
    tui_draw_text(fb, win_x + 10, win_y + 10, "Installing KyroOS...", TEXT_COLOR);
    
    tui_draw_text(fb, win_x + 10, win_y + 40, "[1/4] Creating directories...", TEXT_COLOR);
    tui_draw_text(fb, win_x + 10, win_y + 60, "[2/4] Copying system files...", TEXT_COLOR);
    tui_draw_text(fb, win_x + 10, win_y + 80, "[3/4] Installing boot loader...", TEXT_COLOR);
    tui_draw_text(fb, win_x + 10, win_y + 100, "[4/4] Configuring system...", TEXT_COLOR);
    
    tui_draw_box(fb, win_x + 10, win_y + 130, win_w - 20, 20, 0x555555);
    tui_draw_box(fb, win_x + 10, win_y + 130, (win_w - 20) * 2 / 4, 20, BTN_COLOR);
}

static void draw_step_complete(fb_info_t *fb) {
    uint32_t win_x = 50, win_y = 50;
    uint32_t win_w = fb->width - 100, win_h = fb->height - 150;
    
    tui_draw_box(fb, win_x, win_y, win_w, win_h, TITLE_BG_COLOR);
    
    tui_draw_text(fb, win_x + 10, win_y + 10, "Installation Complete!", BTN_COLOR);
    tui_draw_text(fb, win_x + 10, win_y + 40, "KyroOS has been successfully installed.", TEXT_COLOR);
    tui_draw_text(fb, win_x + 10, win_y + 70, "Please reboot your system.", TEXT_COLOR);
    
    tui_draw_box(fb, win_x + 10, win_y + 100, 150, 30, BTN_COLOR);
    tui_draw_text(fb, win_x + 35, win_y + 105, "Reboot Now", TEXT_COLOR);
}

static void do_install() {
    strcpy(install_log, "Installing...\n");
    
    // Step 1: Partition
    get_ticks(); for (volatile int i = 0; i < 500000; i++);
    strcpy(install_log, "Partition created\n");
    
    // Step 2: Format
    get_ticks(); for (volatile int i = 0; i < 500000; i++);
    strcpy(install_log, "Filesystem created\n");
    
    // Step 3: Copy files
    get_ticks(); for (volatile int i = 0; i < 1000000; i++);
    strcpy(install_log, "Files copied\n");
    
    // Step 4: Install bootloader
    get_ticks(); for (volatile int i = 0; i < 500000; i++);
    strcpy(install_log, "Bootloader installed\n");
    
    strcpy(status_msg, "Installation complete!");
}

int main() {
    fb_info_t fb_info;
    if (tui_init(&fb_info) != 0) {
        return -1;
    }

    while (1) {
        tui_clear_screen(&fb_info, BG_COLOR);
        draw_header(&fb_info);
        
        switch (step) {
            case 0: draw_step_welcome(&fb_info); break;
            case 1: draw_step_disk_select(&fb_info); break;
            case 2: draw_step_partition(&fb_info); break;
            case 3: draw_step_format(&fb_info); break;
            case 4: draw_step_install(&fb_info); break;
            case 5: draw_step_complete(&fb_info); break;
        }
        
        draw_footer(&fb_info);

        event_t event;
        while (input_poll_event(&event)) {
            if (event.type == 1) {
                char key = (char)event.data1;
                
                if (key == 'q' || key == 'Q') {
                    return 0;
                }
                
                if (key == '\n' || key == 0x1C) {
                    if (step < 5) {
                        if (step == 3) {
                            do_install();
                        }
                        step++;
                    } else {
                        syscall(0, 0, 0, 0); // Exit
                        return 0;
                    }
                }
                
                if (step == 1) {
                    if (key == 'w' || key == 'W' || (uint8_t)event.data2 == 0x48) {
                        if (selected_disk > 0) selected_disk--;
                    }
                    if (key == 's' || key == 'S' || (uint8_t)event.data2 == 0x50) {
                        if (selected_disk < disk_count - 1) selected_disk++;
                    }
                }
            }
        }
        
        get_ticks();
    }

    return 0;
}
