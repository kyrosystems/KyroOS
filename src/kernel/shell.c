#include "shell.h"
#include "event.h"
#include "fb.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "vfs.h"
#include "scheduler.h"
#include "port_io.h"
#include "keyboard.h"
#include "net.h"
#include "rtc.h"
#include "isr.h"
#include "ip.h"
#include "icmp.h"
#include "dhcp.h"
#include "pmm.h"
#include "pci.h"
#include "lkm.h"
#include "syscall.h"
#include "vmm.h"
#include "thread.h"
#include "elf.h"
#include <stdint.h>
#include <stddef.h>

extern uint64_t kernel_hhdm_offset;

#define __cpuid(level, a, b, c, d)			\
  __asm__ __volatile__ ("cpuid"				\
			: "=a" (a), "=b" (b), "=c" (c), "=d" (d) \
			: "0" (level))

#define abs(x) ((x) < 0 ? -(x) : (x))
#define SHELL_BUFFER_SIZE 256
#define MAX_ARGS 16
#define SHELL_MAX_FILES 128
#define HISTORY_MAX 16

static char line_buffer[SHELL_BUFFER_SIZE];
static int buffer_index = 0;
static int cursor_pos = 0;
static vfs_node_t *cwd_node = NULL;
static char cwd_path[256];

static char history[HISTORY_MAX][SHELL_BUFFER_SIZE];
static int history_count = 0;
static int history_index = -1;
static bool interrupt_triggered = false;

// --- Colors (Pure Internal) ---
#define COLOR_WHITE  0xFFFFFFFF
#define COLOR_GREEN  0xFF55FF55
#define COLOR_BLUE   0xFF5555FF
#define COLOR_RED    0xFFFF5555
#define COLOR_CYAN   0xFF55FFFF
#define COLOR_RESET  0xFFFFFFFF

#define OUT_BUF_SIZE 4096
static char out_buf[OUT_BUF_SIZE];
static int out_buf_idx = 0;

static void flush_out() {
    if (out_buf_idx > 0) {
        out_buf[out_buf_idx] = '\0';
        klog_print_str(out_buf, false);
        out_buf_idx = 0;
    }
}

static void kprintf(const char *fmt, ...) {
    char buf[1024]; va_list args; va_start(args, fmt); vksprintf(buf, fmt, args); va_end(args);
    int len = strlen(buf);
    if (out_buf_idx + len >= OUT_BUF_SIZE - 1) flush_out();
    if (len < OUT_BUF_SIZE) { memcpy(out_buf + out_buf_idx, buf, len); out_buf_idx += len; }
    else klog_print_str(buf, false);
}

static void net_poll_all() {
    net_dev_t *dev = network_devices;
    while (dev) { if (dev->receive_packet) dev->receive_packet(dev, NULL, 0); dev = dev->next; }
}

static bool check_interrupt() {
    event_t ev; while (event_pop(&ev)) {
        if (ev.type == EVENT_KEY_DOWN) {
            if (keyboard_is_ctrl_pressed() && ((char)ev.data1 == 'c' || (char)ev.data1 == 'C')) {
                interrupt_triggered = true; return true;
            }
        }
    }
    return false;
}

static uint32_t parse_ip(const char *ip_str) {
    int parts[4] = {0}; int current = 0; const char *p = ip_str;
    while (*p) {
        if (*p == '.') { current++; if (current > 3) return 0; }
        else if (*p >= '0' && *p <= '9') { parts[current] = parts[current] * 10 + (*p - '0'); if (parts[current] > 255) return 0; }
        else return 0;
        p++;
    }
    return (current != 3) ? 0 : (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
}

static void canonicalize_path(const char *input, char *output) {
    char combined[512];
    if (input[0] == '/') strncpy(combined, input, 512);
    else ksprintf(combined, "%s/%s", cwd_path, input);
    char stack[16][64]; int top = 0; char temp[512]; strncpy(temp, combined, 512); char *p = temp;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (!*p) break;
        char *start = p; while (*p && *p != '/') p++;
        size_t len = p - start; if (len > 63) len = 63;
        char token[64]; memcpy(token, start, len); token[len] = '\0';
        if (strcmp(token, ".") == 0) continue;
        if (strcmp(token, "..") == 0) { if (top > 0) top--; continue; }
        strncpy(stack[top++], token, 64); if (top >= 16) break;
    }
    strncpy(output, "/", 256);
    for (int i = 0; i < top; i++) { if (i > 0 || output[1] != '\0') strncat(output, "/", 256); strncat(output, stack[i], 256); }
}

static void cmd_ls(int argc, char **argv) {
    flush_out(); vfs_node_t *dir = cwd_node;
    if (argc > 1) { char target[256]; canonicalize_path(argv[1], target); dir = vfs_resolve_path(vfs_root, target); }
    if (!dir) { kprintf("ls: directory not found\n"); return; }
    struct dirent de; int count = 0; size_t max_len = 0;
    while (vfs_readdir(dir, count, &de) == 1 && count < SHELL_MAX_FILES) {
        size_t l = strlen(de.name); if (l > max_len) max_len = l; count++;
    }
    int cols = 80 / (max_len + 2); if (cols == 0) cols = 1;
    for (int i = 0; i < count; i++) {
        vfs_readdir(dir, i, &de); vfs_node_t *node = vfs_finddir(dir, de.name);
        uint32_t color = COLOR_CYAN;
        if (node) {
            if (node->flags & VFS_DIRECTORY) color = COLOR_WHITE;
            else if (strstr(de.name, ".bin") || strstr(de.name, ".elf")) color = COLOR_GREEN;
            else if (strstr(de.name, ".kpkg")) color = COLOR_RED;
        }
        log_set_fg_color(color); kprintf("%s", de.name); log_set_fg_color(COLOR_RESET);
        for (size_t p = 0; p < (max_len + 2 - strlen(de.name)); p++) kprintf(" ");
        if ((i + 1) % cols == 0) kprintf("\n");
    }
    kprintf("\n");
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) return;
    vfs_node_t *n = vfs_resolve_path(cwd_node, argv[1]);
    if (n) { char b[1024]; uint32_t r, o=0; while((r=vfs_read(n, o, 1023, (uint8_t*)b))>0){b[r]='\0'; klog_print_str(b, false); o+=r;} klog_putchar('\n'); }
    else kprintf("cat: file not found\n");
}

static void cmd_edit(int argc, char **argv) {
    if (argc < 2) return;
    char *filename = argv[1]; vfs_node_t *node = vfs_resolve_path(cwd_node, filename);
    if (!node) { vfs_create(cwd_node, filename, 0); node = vfs_resolve_path(cwd_node, filename); }
    char *buffer = (char*)kmalloc(8192); memset(buffer, 0, 8192); vfs_read(node, 0, 8191, (uint8_t*)buffer);
    int pos = strlen(buffer);
    auto void redraw() { console_clear(); klog_print_str(buffer, false); log_set_fg_color(COLOR_CYAN); klog_print_str("\n--- NanoEdit: ^O Save, ^X Exit ---\n", true); log_set_fg_color(COLOR_RESET); };
    redraw();
    while (1) {
        event_t ev; event_wait(&ev);
        if (ev.type == EVENT_KEY_DOWN) {
            char c = (char)ev.data1;
            if (keyboard_is_ctrl_pressed()) {
                if (c == 'x' || c == 'X') break;
                if (c == 'o' || c == 'O') { vfs_write(node, 0, strlen(buffer), (uint8_t*)buffer); redraw(); kprintf("[Saved]"); flush_out(); fb_flush(); continue; }
            }
            if (c == '\b' && pos > 0) { buffer[--pos] = '\0'; redraw(); }
            else if (c >= 32 || c == '\n') { if (pos < 8191) { buffer[pos++] = c; buffer[pos] = '\0'; klog_putchar(c); } }
            fb_flush();
        }
    }
    kfree(buffer); console_clear();
}

static int isin(int d) { d %= 360; if (d < 0) d += 360; static const int16_t s[91] = {0,17,34,52,69,87,104,121,139,156,173,190,207,224,241,258,275,292,309,325,342,358,374,390,406,422,438,453,469,484,499,515,529,544,559,573,587,601,615,629,642,656,669,682,694,707,719,731,743,754,766,777,788,798,809,819,829,838,848,857,866,874,882,891,898,906,913,920,927,933,939,945,951,956,961,965,970,974,978,981,984,987,990,992,994,996,997,998,999,999,1000}; if (d <= 90) return s[d]; if (d <= 180) return s[180 - d]; if (d <= 270) return -s[d - 180]; return -s[360 - d]; }
static int icos(int d) { return isin(d + 90); }
static void draw_line(int x0, int y0, int x1, int y1, uint32_t c) { int dx=abs(x1-x0), sx=x0<x1?1:-1, dy=-abs(y1-y0), sy=y0<y1?1:-1, err=dx+dy, e2; while(1){fb_set_pixel(x0,y0,c); if(x0==x1&&y0==y1)break; e2=2*err; if(e2>=dy){err+=dy;x0+=sx;} if(e2<=dx){err+=dx;y0+=sy;}} }

static void cmd_3d() {
    event_t ev;
    while(event_pop(&ev)); // Clear queue properly
    console_clear();
    kprintf("3D Cube (Press 'Q', 'ESC', Ctrl+C/D to exit)\n");
    flush_out();
    fb_flush();

    int16_t cube[8][3] = {
        {-100,-100,-100}, {100,-100,-100}, {100,100,-100}, {-100,100,-100},
        {-100,-100,100}, {100,-100,100}, {100,100,100}, {-100,100,100}
    };
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    int angle = 0;
    bool running = true;
    while (running) {
        while (event_pop(&ev)) {
            if (ev.type == EVENT_KEY_DOWN) {
                char c = (char)ev.data1;
                if (c == 'q' || c == 'Q' || ev.data2 == 0x01) { // 'q' or ESC
                    running = false;
                    break;
                }
                if (keyboard_is_ctrl_pressed() && (c == 'c' || c == 'C' || c == 'd' || c == 'D')) {
                    running = false;
                    break;
                }
            }
        }
        if (!running) break;

        fb_clear(0);
        angle = (angle + 5) % 360;
        int s = isin(angle), c = icos(angle);
        for (int i = 0; i < 12; i++) {
            int x1_raw = cube[edges[i][0]][0], y1_raw = cube[edges[i][0]][1], z1_raw = cube[edges[i][0]][2];
            int x2_raw = cube[edges[i][1]][0], y2_raw = cube[edges[i][1]][1], z2_raw = cube[edges[i][1]][2];

            // Rotate around Y
            int x1 = (x1_raw * c - z1_raw * s) / 1000, z1 = (x1_raw * s + z1_raw * c) / 1000;
            int x2 = (x2_raw * c - z2_raw * s) / 1000, z2 = (x2_raw * s + z2_raw * c) / 1000;

            // Simple perspective projection
            int px1 = 512 + (x1 * 400 / (z1 + 500)), py1 = 384 + (y1_raw * 400 / (z1 + 500));
            int px2 = 512 + (x2 * 400 / (z2 + 500)), py2 = 384 + (y2_raw * 400 / (z2 + 500));
            draw_line(px1, py1, px2, py2, 0x00FF00);
        }
        fb_flush();
        for(volatile int k=0; k<1000000; k++);
    }
    console_clear();
}

static void get_cpu_model(char *model) {
    uint32_t regs[4];
    for (int i = 0; i < 3; i++) {
        __cpuid(0x80000002 + i, regs[0], regs[1], regs[2], regs[3]);
        memcpy(model + i * 16, regs, 16);
    }
    model[48] = '\0';
}

static void cmd_kyrofetch() {
    char cpu_model[64];
    get_cpu_model(cpu_model);
    uint64_t total_mem = pmm_get_total_memory();
    uint64_t used_mem = pmm_get_used_memory();
    uint32_t ip = ip_get_local_ip();

    kprintf("KyroOS Titanium %s\n", OS_VERSION);
    kprintf("------------------------------\n");
    kprintf("CPU:     %s\n", cpu_model);
    kprintf("RAM:     %llu MB / %llu MB\n", used_mem / 1024 / 1024, total_mem / 1024 / 1024);
    kprintf("IP:      %d.%d.%d.%d\n", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    kprintf("Uptime:  %llu seconds\n", timer_get_ticks() / 100);
    kprintf("Shell:   KShell v3.2\n");
}

static void cmd_free() {
    uint64_t total = pmm_get_total_memory();
    uint64_t used = pmm_get_used_memory();
    kprintf("              total        used        free\n");
    kprintf("Mem:    %10llu  %10llu  %10llu\n", total, used, total - used);
}

static void cmd_lspci() {
    pci_device_node_t *node = pci_devices;
    while (node) {
        pci_device_t *dev = &node->device;
        kprintf("%02x:%02x.%d %04x:%04x (Class %02x%02x)\n", 
                dev->bus, dev->device, dev->func, dev->vendor_id, dev->device_id, 
                dev->class_code, dev->subclass);
        node = node->next;
    }
}

static void cmd_ps() {
    extern void scheduler_print_threads();
    scheduler_print_threads();
}

// Execute userspace binary from /bin/
static void cmd_exec_bin(const char *name, int argc, char **argv) {
    char path[256];
    ksprintf(path, "/bin/%s", name);

    vfs_node_t *node = vfs_resolve_path(vfs_root, path);
    if (!node) {
        kprintf("%s: command not found\n", name);
        return;
    }

    // Read ELF header
    uint8_t elf_header[64];
    if (vfs_read(node, 0, 64, elf_header) != 64) {
        kprintf("%s: failed to read ELF header\n", name);
        return;
    }

    // Verify ELF magic
    if (elf_header[0] != 0x7F || elf_header[1] != 'E' || 
        elf_header[2] != 'L' || elf_header[3] != 'F') {
        kprintf("%s: not a valid ELF file\n", name);
        return;
    }

    // Load entire file into memory
    uint8_t *elf_data = (uint8_t *)kmalloc(node->length);
    if (!elf_data) {
        kprintf("%s: failed to allocate memory\n", name);
        return;
    }

    if (vfs_read(node, 0, node->length, elf_data) != node->length) {
        kprintf("%s: failed to read ELF file\n", name);
        kfree(elf_data);
        return;
    }

    // Parse ELF
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_data;
    Elf64_Phdr *phdr = (Elf64_Phdr *)(elf_data + ehdr->e_phoff);

    // Allocate new PML4
    pml4_t *new_pml4 = vmm_create_address_space();
    if (!new_pml4) {
        kprintf("%s: failed to create address space\n", name);
        kfree(elf_data);
        return;
    }

    // Load program headers
    uint64_t entry_point = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint64_t vaddr = phdr[i].p_vaddr & ~0xFFF;
            uint64_t end_vaddr = (phdr[i].p_vaddr + phdr[i].p_memsz + 0xFFF) & ~0xFFF;

            for (uint64_t addr = vaddr; addr < end_vaddr; addr += PAGE_SIZE) {
                void *phys = pmm_alloc_page();
                if (!phys) {
                    kprintf("%s: failed to allocate page\n", name);
                    vmm_destroy_address_space(new_pml4);
                    kfree(elf_data);
                    return;
                }
                memset(phys, 0, PAGE_SIZE);
                vmm_map_page(new_pml4, (void *)(addr + kernel_hhdm_offset), phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
            }

            uint8_t *vaddr_ptr = (uint8_t *)(phdr[i].p_vaddr + kernel_hhdm_offset);
            memcpy(vaddr_ptr, elf_data + phdr[i].p_offset, phdr[i].p_filesz);

            if (i == 0) entry_point = ehdr->e_entry;
        }
    }

    // Allocate user stack
    uint64_t stack_top = 0x8000000000;
    for (uint64_t addr = stack_top - (8 * PAGE_SIZE); addr < stack_top; addr += PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            kprintf("%s: failed to allocate stack\n", name);
            vmm_destroy_address_space(new_pml4);
            kfree(elf_data);
            return;
        }
        memset(phys, 0, PAGE_SIZE);
        vmm_map_page(new_pml4, (void *)(addr + kernel_hhdm_offset), phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }

    kfree(elf_data);

    // Create thread
    thread_t *new_thread = thread_create_userspace(entry_point, new_pml4, 0, 0, NULL);
    if (!new_thread) {
        kprintf("%s: failed to create thread\n", name);
        vmm_destroy_address_space(new_pml4);
        return;
    }

    kprintf("Started: %s (PID: %d)\n", name, (int)new_thread->id);
}

static void cmd_lsmod() {
    lkm_list_modules(&g_lkm_manager);
}

static void cmd_insmod(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: insmod <module.kmod>\n");
        return;
    }
    if (lkm_load_module(&g_lkm_manager, argv[1]) == 0) {
        kprintf("Module '%s' loaded successfully.\n", argv[1]);
    } else {
        kprintf("Failed to load module '%s'.\n", argv[1]);
    }
}

static void cmd_rmmod(int argc, char **argv) {
    if (argc < 2) {
        kprintf("Usage: rmmod <module_name>\n");
        return;
    }
    if (lkm_unload_module(&g_lkm_manager, argv[1]) == 0) {
        kprintf("Module '%s' unloaded successfully.\n", argv[1]);
    } else {
        kprintf("Failed to unload module '%s'.\n", argv[1]);
    }
}

static void execute_command(char *cmd) {
    if (strlen(cmd) == 0) return;
    if (history_count < HISTORY_MAX) strncpy(history[history_count++], cmd, SHELL_BUFFER_SIZE);
    else { for(int i=0; i<HISTORY_MAX-1; i++) memcpy(history[i], history[i+1], SHELL_BUFFER_SIZE); strncpy(history[HISTORY_MAX-1], cmd, SHELL_BUFFER_SIZE); }
    history_index = history_count;
    char *argv[MAX_ARGS]; int argc = 0; char *p = cmd;
    while (*p && argc < MAX_ARGS) { while (*p == ' ') p++; if (!*p) break; argv[argc++] = p; while (*p && *p != ' ') p++; if (*p) *p++ = '\0'; }
    if (argc == 0) {
        return;
    }
    interrupt_triggered = false;
    
    if (strcmp(argv[0], "help") == 0) {
        kprintf("File ops: ls, cat, cd, touch, mkdir, rm, rmdir, cp, mv, ln, chmod, find\n");
        kprintf("Text:     grep, head, tail, wc, sort, uniq\n");
        kprintf("System:   ps, kill, killall, nice, renice, sleep, date, time, uptime\n");
        kprintf("Disk:     df, du, mount, umount, fdisk, mkfs\n");
        kprintf("Info:     uname, hostname, whoami, id, groups, users, kyrofetch, lspci, free\n");
        kprintf("Shell:    echo, env, export, unset, alias, unalias, history, clear, reset\n");
        kprintf("Power:    reboot, halt, poweroff\n");
        kprintf("Net:      ping, ifconfig, dhcp, wget\n");
        kprintf("Drivers:  lsmod, insmod, rmmod\n");
        kprintf("Other:    edit, 3d, game, pwd, true, false, test, yes, sync, help\n");
    }
    else if (strcmp(argv[0], "ls") == 0) cmd_ls(argc, argv);
    else if (strcmp(argv[0], "cat") == 0) cmd_cat(argc, argv);
    else if (strcmp(argv[0], "cd") == 0) {
        char target[256]; if (argc<2) strncpy(target, "/", 256); else canonicalize_path(argv[1], target);
        vfs_node_t *n = vfs_resolve_path(vfs_root, target); if (n) { cwd_node = n; strncpy(cwd_path, target, 256); } else kprintf("cd: no such directory\n");
    }
    // File operations - execute from /bin/
    else if (strcmp(argv[0], "touch") == 0) cmd_exec_bin("touch", argc, argv);
    else if (strcmp(argv[0], "mkdir") == 0) cmd_exec_bin("mkdir", argc, argv);
    else if (strcmp(argv[0], "rm") == 0) cmd_exec_bin("rm", argc, argv);
    else if (strcmp(argv[0], "rmdir") == 0) cmd_exec_bin("rmdir", argc, argv);
    else if (strcmp(argv[0], "cp") == 0) cmd_exec_bin("cp", argc, argv);
    else if (strcmp(argv[0], "mv") == 0) cmd_exec_bin("mv", argc, argv);
    else if (strcmp(argv[0], "ln") == 0) cmd_exec_bin("ln", argc, argv);
    else if (strcmp(argv[0], "chmod") == 0) cmd_exec_bin("chmod", argc, argv);
    else if (strcmp(argv[0], "find") == 0) cmd_exec_bin("find", argc, argv);
    // Text processing
    else if (strcmp(argv[0], "grep") == 0) cmd_exec_bin("grep", argc, argv);
    else if (strcmp(argv[0], "head") == 0) cmd_exec_bin("head", argc, argv);
    else if (strcmp(argv[0], "tail") == 0) cmd_exec_bin("tail", argc, argv);
    else if (strcmp(argv[0], "wc") == 0) cmd_exec_bin("wc", argc, argv);
    else if (strcmp(argv[0], "sort") == 0) cmd_exec_bin("sort", argc, argv);
    else if (strcmp(argv[0], "uniq") == 0) cmd_exec_bin("uniq", argc, argv);
    // System
    else if (strcmp(argv[0], "kill") == 0) cmd_exec_bin("kill", argc, argv);
    else if (strcmp(argv[0], "killall") == 0) cmd_exec_bin("killall", argc, argv);
    else if (strcmp(argv[0], "nice") == 0) cmd_exec_bin("nice", argc, argv);
    else if (strcmp(argv[0], "renice") == 0) cmd_exec_bin("renice", argc, argv);
    else if (strcmp(argv[0], "sleep") == 0) cmd_exec_bin("sleep", argc, argv);
    else if (strcmp(argv[0], "date") == 0) cmd_exec_bin("date", argc, argv);
    else if (strcmp(argv[0], "time") == 0) cmd_exec_bin("time", argc, argv);
    // Disk
    else if (strcmp(argv[0], "df") == 0) cmd_exec_bin("df", argc, argv);
    else if (strcmp(argv[0], "du") == 0) cmd_exec_bin("du", argc, argv);
    else if (strcmp(argv[0], "mount") == 0) cmd_exec_bin("mount", argc, argv);
    else if (strcmp(argv[0], "umount") == 0) cmd_exec_bin("umount", argc, argv);
    else if (strcmp(argv[0], "fdisk") == 0) cmd_exec_bin("fdisk", argc, argv);
    else if (strcmp(argv[0], "mkfs") == 0) cmd_exec_bin("mkfs", argc, argv);
    // Info
    else if (strcmp(argv[0], "uname") == 0) cmd_exec_bin("uname", argc, argv);
    else if (strcmp(argv[0], "hostname") == 0) cmd_exec_bin("hostname", argc, argv);
    else if (strcmp(argv[0], "id") == 0) cmd_exec_bin("id", argc, argv);
    else if (strcmp(argv[0], "groups") == 0) cmd_exec_bin("groups", argc, argv);
    else if (strcmp(argv[0], "users") == 0) cmd_exec_bin("users", argc, argv);
    // Shell
    else if (strcmp(argv[0], "echo") == 0) cmd_exec_bin("echo", argc, argv);
    else if (strcmp(argv[0], "env") == 0) cmd_exec_bin("env", argc, argv);
    else if (strcmp(argv[0], "export") == 0) cmd_exec_bin("export", argc, argv);
    else if (strcmp(argv[0], "unset") == 0) cmd_exec_bin("unset", argc, argv);
    else if (strcmp(argv[0], "alias") == 0) cmd_exec_bin("alias", argc, argv);
    else if (strcmp(argv[0], "unalias") == 0) cmd_exec_bin("unalias", argc, argv);
    else if (strcmp(argv[0], "history") == 0) cmd_exec_bin("history", argc, argv);
    else if (strcmp(argv[0], "clear") == 0) cmd_exec_bin("clear", argc, argv);
    else if (strcmp(argv[0], "reset") == 0) cmd_exec_bin("reset", argc, argv);
    // Power
    else if (strcmp(argv[0], "halt") == 0) cmd_exec_bin("halt", argc, argv);
    else if (strcmp(argv[0], "poweroff") == 0) cmd_exec_bin("poweroff", argc, argv);
    // Net
    else if (strcmp(argv[0], "wget") == 0) cmd_exec_bin("wget", argc, argv);
    // Other
    else if (strcmp(argv[0], "game") == 0) cmd_exec_bin("game", argc, argv);
    else if (strcmp(argv[0], "pwd") == 0) cmd_exec_bin("pwd", argc, argv);
    else if (strcmp(argv[0], "true") == 0) cmd_exec_bin("true", argc, argv);
    else if (strcmp(argv[0], "false") == 0) cmd_exec_bin("false", argc, argv);
    else if (strcmp(argv[0], "test") == 0) cmd_exec_bin("test", argc, argv);
    else if (strcmp(argv[0], "yes") == 0) cmd_exec_bin("yes", argc, argv);
    else if (strcmp(argv[0], "sync") == 0) cmd_exec_bin("sync", argc, argv);
    else if (strcmp(argv[0], "kyrofetch") == 0) cmd_kyrofetch();
    else if (strcmp(argv[0], "edit") == 0) cmd_edit(argc, argv);
    else if (strcmp(argv[0], "3d") == 0) cmd_3d();
    else if (strcmp(argv[0], "free") == 0) cmd_free();
    else if (strcmp(argv[0], "lspci") == 0) cmd_lspci();
    else if (strcmp(argv[0], "ps") == 0) cmd_ps();
    else if (strcmp(argv[0], "lsmod") == 0) cmd_lsmod();
    else if (strcmp(argv[0], "insmod") == 0) cmd_insmod(argc, argv);
    else if (strcmp(argv[0], "rmmod") == 0) cmd_rmmod(argc, argv);
    else if (strcmp(argv[0], "whoami") == 0) kprintf("root\n");
    else if (strcmp(argv[0], "uptime") == 0) { uint64_t s = timer_get_ticks()/100; kprintf("up %lluh %llum %llus\n", s/3600, (s%3600)/60, s%60); }
    else if (strcmp(argv[0], "ifconfig") == 0) {
        net_dev_t *dev = network_devices; if(!dev) kprintf("No interfaces.\n");
        while(dev) { uint32_t ip = ip_get_local_ip(), mask = ip_get_subnet_mask(); kprintf("eth0: MAC %02x:%02x:%02x:%02x:%02x:%02x\n      IP: %d.%d.%d.%d Mask: %d.%d.%d.%d\n", dev->mac_addr[0], dev->mac_addr[1], dev->mac_addr[2], dev->mac_addr[3], dev->mac_addr[4], dev->mac_addr[5], (ip>>24)&0xFF, (ip>>16)&0xFF, (ip>>8)&0xFF, ip&0xFF, (mask>>24)&0xFF, (mask>>16)&0xFF, (mask>>8)&0xFF, mask&0xFF); dev = dev->next; }
    }
    else if (strcmp(argv[0], "ping") == 0 && argc > 1) {
        uint32_t ip = parse_ip(argv[1]); if (ip) for(int i=0; i<4 && !interrupt_triggered; i++) { if(icmp_ping(ip, 1000)) kprintf("Reply from %s: time<1ms\n", argv[1]); else kprintf("Timeout\n"); check_interrupt(); flush_out(); fb_flush(); }
    }
    else if (strcmp(argv[0], "dhcp") == 0) { dhcp_client_start(); kprintf("DHCP Sent.\n"); }
    else if (strcmp(argv[0], "reboot") == 0) outb(0x64, 0xFE);
    else kprintf("Unknown command: %s\n", argv[0]);
    flush_out();
}

void shell_main(void *arg) {
    (void)arg; if (vfs_root) { cwd_node = vfs_root; strncpy(cwd_path, "/", 2); }
    console_clear(); cmd_kyrofetch();
    while (1) {
        net_poll_all();
        // Green '>' prompt using direct log functions
        log_set_fg_color(COLOR_GREEN);
        klog_putchar('>');
        log_set_fg_color(COLOR_RESET);
        kprintf(" %s $ ", cwd_path);
        flush_out(); fb_flush();
        while(1) {
            net_poll_all(); event_t ev; if (event_pop(&ev)) {
                if (ev.type == EVENT_KEY_DOWN) {
                    char c = (char)ev.data1; uint8_t sc = (uint8_t)ev.data2;
                    if (sc == 0x48 && history_index > 0) { history_index--; while(buffer_index > 0) { klog_putchar('\b'); buffer_index--; } strncpy(line_buffer, history[history_index], SHELL_BUFFER_SIZE); buffer_index = strlen(line_buffer); cursor_pos = buffer_index; klog_print_str(line_buffer, false); }
                    else if (sc == 0x50 && history_index < history_count - 1) { history_index++; while(buffer_index > 0) { klog_putchar('\b'); buffer_index--; } strncpy(line_buffer, history[history_index], SHELL_BUFFER_SIZE); buffer_index = strlen(line_buffer); cursor_pos = buffer_index; klog_print_str(line_buffer, false); }
                    else if (sc == 0x4B && cursor_pos > 0) { cursor_pos--; }
                    else if (sc == 0x4D && cursor_pos < buffer_index) { cursor_pos++; }
                    else if (c == '\n') { klog_putchar('\n'); line_buffer[buffer_index] = '\0'; execute_command(line_buffer); buffer_index = 0; cursor_pos = 0; break; }
                    else if (c == '\b' && buffer_index > 0) { klog_putchar('\b'); buffer_index--; cursor_pos--; }
                    else if (c >= 32 && c <= 126 && buffer_index < SHELL_BUFFER_SIZE-1) { klog_putchar(c); line_buffer[buffer_index++] = c; cursor_pos++; }
                    fb_flush();
                }
            }
        }
    }
}
