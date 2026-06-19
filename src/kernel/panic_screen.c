#include "panic_screen.h"
#include "fb.h"
#include "font.h"
#include "kstring.h"
#include "version.h"
#include "isr.h"
#include "port_io.h"
#include "vfs.h"
#include "heap.h"
#include "log.h"
#include "pmm.h"
#include "random.h"
#include "epstein.h"
#include "image.h"

extern int epstein;

#define PANIC_BG_COLOR  0xFFFF0000
#define PANIC_FG_COLOR  0xFFFFFFFF
#define INFO_TEXT_COLOR 0xFFDDDDDD

static char **panic_quotes     = NULL;
static size_t num_panic_quotes = 0;

static void panic_get_cpu_brand(char *buf) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ __volatile__("cpuid" : "=a"(eax) : "a"(0x80000000));
    if (eax < 0x80000004) { strncpy(buf, "Generic x86_64", 48); return; }
    uint32_t *ptr = (uint32_t *)buf;
    for (uint32_t i = 0; i < 3; i++) {
        __asm__ __volatile__("cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(0x80000002 + i));
        ptr[i*4+0]=eax; ptr[i*4+1]=ebx; ptr[i*4+2]=ecx; ptr[i*4+3]=edx;
    }
    buf[47] = '\0';
    char *start = buf;
    while (*start == ' ') start++;
    if (start != buf) { char *dst = buf; while (*start) *dst++ = *start++; *dst = '\0'; }
}

static void panic_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    fb_draw_char(c, x, y, fg, bg);
}

static void panic_print_str(int *x, int *y, const char *s, uint32_t fg, uint32_t bg) {
    const fb_info_t *fb_info = fb_get_info();
    if (!fb_info || !s) return;
    int start_x = *x;
    while (*s) {
        char c = *s++;
        if (c == '\n') { *y += FONT_HEIGHT; *x = start_x; continue; }
        if (*x + FONT_WIDTH > (int)fb_info->width) { *y += FONT_HEIGHT; *x = start_x; }
        panic_draw_char(*x, *y, c, fg, bg);
        *x += FONT_WIDTH;
    }
}

static void print_page_fault_error(int *x, int *y, uint64_t err, uint32_t fg, uint32_t bg) {
    char buf[100];
    ksprintf(buf, "Page Fault Error Code: %016lx", err);
    panic_print_str(x, y, buf, fg, bg); *y += FONT_HEIGHT; *x = 20;
    panic_print_str(x, y, (err & 1) ? " P=1 Protection Violation" : " P=0 Page Not Present",  fg, bg); *y += FONT_HEIGHT; *x = 20;
    panic_print_str(x, y, (err & 2) ? " W=1 Write"                : " W=0 Read",               fg, bg); *y += FONT_HEIGHT; *x = 20;
    panic_print_str(x, y, (err & 4) ? " U=1 User Mode"            : " U=0 Supervisor Mode",    fg, bg); *y += FONT_HEIGHT; *x = 20;
    if (err & 8)  { panic_print_str(x, y, " RSVD=1 Reserved Bit Set", fg, bg); *y += FONT_HEIGHT; *x = 20; }
    if (err & 16) { panic_print_str(x, y, " ID=1 Instruction Fetch",  fg, bg); *y += FONT_HEIGHT; *x = 20; }
}

static void print_stack_trace(int *x, int *y, uint64_t rbp, uint32_t fg, uint32_t bg) {
    const fb_info_t *fb_info = fb_get_info();
    int max_y = fb_info ? (int)fb_info->height - FONT_HEIGHT * 4 : 9999;

    panic_print_str(x, y, "--- Stack Trace (RBP chain) ---", fg, bg);
    *y += FONT_HEIGHT; *x = 20;

    uint64_t *frame = (uint64_t *)rbp;
    for (int i = 0; i < 16 && frame; i++) {
        if (*y >= max_y) break; 
        if ((uint64_t)frame < 0xffff800000000000ULL) break;
        uint64_t rip = frame[1];
        if (!rip) break;
        char buf[48];
        ksprintf(buf, "  #%d  0x%016lx", i, rip);
        panic_print_str(x, y, buf, fg, bg);
        *y += FONT_HEIGHT; *x = 20;
        frame = (uint64_t *)frame[0];
    }
}

void panic_screen_init(void) {
    vfs_node_t *file_node = vfs_resolve_path(vfs_root, "/panic_quotes.txt");
    if (!file_node) return;
    struct stat st;
    if (!file_node->stat || file_node->stat(file_node, &st) < 0) return;
    char *fc = (char *)kmalloc(st.st_size + 1);
    if (!fc) return;
    if (!file_node->read || file_node->read(file_node, 0, st.st_size, (uint8_t *)fc) != st.st_size) {
        kfree(fc); return;
    }
    fc[st.st_size] = '\0';

    size_t n = 0;
    for (char *p = fc; *p; p++) if (*p == '\n') n++;
    if (!n) { kfree(fc); return; }

    panic_quotes = (char **)kmalloc(n * sizeof(char *));
    if (!panic_quotes) { kfree(fc); return; }

    char *ptr = fc;
    size_t idx = 0;
    while (*ptr && idx < n) {
        char *eol = strchr(ptr, '\n');
        size_t len = eol ? (size_t)(eol - ptr) : strlen(ptr);
        panic_quotes[idx] = (char *)kmalloc(len + 1);
        if (panic_quotes[idx]) {
            strncpy(panic_quotes[idx], ptr, len);
            panic_quotes[idx][len] = '\0';
            idx++;
        }
        ptr = eol ? eol + 1 : ptr + len;
    }
    num_panic_quotes = idx;
    kfree(fc);
}

void panic_screen_show(const char *message, struct registers *regs) {
    __asm__ volatile("cli");

    fb_clear(PANIC_BG_COLOR);
    fb_flush();

    const fb_info_t *fb_info = fb_get_info();
    if (!fb_info) { for (;;) __asm__ volatile("cli; hlt"); }

    int x = 20, y = 20;
    char info_buf[160];

    panic_print_str(&x, &y, "!!! KERNEL PANIC !!!", PANIC_FG_COLOR, PANIC_BG_COLOR);
    y += FONT_HEIGHT; x = 20;
    panic_print_str(&x, &y, message ? message : "(no message)", PANIC_FG_COLOR, PANIC_BG_COLOR);
    y += FONT_HEIGHT * 2; x = 20;
    fb_flush(); 

    panic_print_str(&x, &y, "[CP2: sysinfo]", INFO_TEXT_COLOR, PANIC_BG_COLOR);
    y += FONT_HEIGHT; x = 20;
    fb_flush();

    int rc = (int)fb_info->width - 380;
    int rx = rc, ry = 20;
    panic_print_str(&rx, &ry, "--- System Info ---", INFO_TEXT_COLOR, PANIC_BG_COLOR);
    ry += FONT_HEIGHT; rx = rc;
    ksprintf(info_buf, "KyroOS %s build %s", KYROOS_VERSION_STRING, KYROOS_VERSION_BUILD);
    panic_print_str(&rx, &ry, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
    ry += FONT_HEIGHT; rx = rc;
    char cpu_brand[48]; panic_get_cpu_brand(cpu_brand);
    ksprintf(info_buf, "CPU: %s", cpu_brand);
    panic_print_str(&rx, &ry, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
    ry += FONT_HEIGHT; rx = rc;
    ksprintf(info_buf, "RAM: %llu / %llu MB", pmm_get_used_memory()/1024/1024, pmm_get_total_memory()/1024/1024);
    panic_print_str(&rx, &ry, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
    ry += FONT_HEIGHT; rx = rc;
    ksprintf(info_buf, "FB: %ux%u @%ubpp", fb_info->width, fb_info->height, fb_info->bpp);
    panic_print_str(&rx, &ry, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
    ry += FONT_HEIGHT; rx = rc;
    uint64_t ticks = timer_get_ticks();
    ksprintf(info_buf, "Uptime: %llu ticks (~%llu s)", ticks, ticks / 1000);
    panic_print_str(&rx, &ry, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
    fb_flush(); // CP3

    panic_print_str(&x, &y, "[CP4: quote]", INFO_TEXT_COLOR, PANIC_BG_COLOR);
    y += FONT_HEIGHT; x = 20;
    fb_flush();

    if (panic_quotes && num_panic_quotes > 0) {
        uint32_t qi = (uint32_t)(random_get_uint64() % num_panic_quotes);
        panic_print_str(&x, &y, "\"", INFO_TEXT_COLOR, PANIC_BG_COLOR);
        panic_print_str(&x, &y, panic_quotes[qi], INFO_TEXT_COLOR, PANIC_BG_COLOR);
        panic_print_str(&x, &y, "\"", INFO_TEXT_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT * 2; x = 20;
    }
    fb_flush(); // CP5

    panic_print_str(&x, &y, "[CP6: logs]", INFO_TEXT_COLOR, PANIC_BG_COLOR);
    y += FONT_HEIGHT; x = 20;
    fb_flush();

    panic_print_str(&x, &y, "--- Last 5 Log Messages ---", INFO_TEXT_COLOR, PANIC_BG_COLOR);
    y += FONT_HEIGHT; x = 20;
    char log_entries[5][LOG_MESSAGE_MAX_LEN];
    int log_count = 0;
    log_get_entries((char *)log_entries, &log_count, 5);
    for (int i = 0; i < log_count; i++) {
        panic_print_str(&x, &y, log_entries[i], PANIC_FG_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT; x = 20;
    }
    y += FONT_HEIGHT;
    fb_flush(); // CP7

    panic_print_str(&x, &y, "[CP8: regs]", INFO_TEXT_COLOR, PANIC_BG_COLOR);
    y += FONT_HEIGHT * 2; x = 20;
    fb_flush();

    if (regs) {
        if (regs->int_no == 14) {
            uint64_t cr2;
            __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
            ksprintf(info_buf, "CR2 (faulting addr): %016lx", cr2);
            panic_print_str(&x, &y, info_buf, 0xFFFFFF00, PANIC_BG_COLOR);
            y += FONT_HEIGHT; x = 20;
            print_page_fault_error(&x, &y, regs->err_code, PANIC_FG_COLOR, PANIC_BG_COLOR);
            y += FONT_HEIGHT;
        }
        fb_flush(); // CP9

        panic_print_str(&x, &y, "--- Registers ---", INFO_TEXT_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT; x = 20;
        int half = (int)fb_info->width / 2 - 20;

        #define REGPAIR(n1, v1, n2, v2) \
            ksprintf(info_buf, n1 "=%016lx", (uint64_t)(v1)); \
            panic_print_str(&x, &y, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR); \
            x = 20 + half; \
            ksprintf(info_buf, n2 "=%016lx", (uint64_t)(v2)); \
            panic_print_str(&x, &y, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR); \
            y += FONT_HEIGHT; x = 20;

        REGPAIR("RAX", regs->rax, "RBX", regs->rbx)
        REGPAIR("RCX", regs->rcx, "RDX", regs->rdx)
        REGPAIR("RSI", regs->rsi, "RDI", regs->rdi)
        REGPAIR("RBP", regs->rbp, "RSP", regs->rsp)
        REGPAIR("R8 ", regs->r8,  "R9 ", regs->r9)
        REGPAIR("R10", regs->r10, "R11", regs->r11)
        REGPAIR("R12", regs->r12, "R13", regs->r13)
        REGPAIR("R14", regs->r14, "R15", regs->r15)
        #undef REGPAIR

        ksprintf(info_buf, "RIP=%016lx  RFLAGS=%016lx", regs->rip, regs->rflags);
        panic_print_str(&x, &y, info_buf, 0xFFFFFF44, PANIC_BG_COLOR);
        y += FONT_HEIGHT; x = 20;
        ksprintf(info_buf, "CS=%04lx  SS=%04lx  INT=%02x  ERR=%04lx",
            (uint64_t)regs->cs, (uint64_t)regs->ss,
            (uint64_t)regs->int_no, regs->err_code);
        panic_print_str(&x, &y, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT * 2; x = 20;
        fb_flush(); // CP10

        // RFLAGS decode
        panic_print_str(&x, &y, "--- RFLAGS ---", INFO_TEXT_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT; x = 20;
        uint64_t fl = regs->rflags;
        ksprintf(info_buf, "CF=%d PF=%d AF=%d ZF=%d SF=%d TF=%d IF=%d DF=%d OF=%d IOPL=%d",
            (int)(fl>>0&1), (int)(fl>>2&1), (int)(fl>>4&1), (int)(fl>>6&1),
            (int)(fl>>7&1), (int)(fl>>8&1), (int)(fl>>9&1), (int)(fl>>10&1),
            (int)(fl>>11&1),(int)(fl>>12&3));
        panic_print_str(&x, &y, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT * 2; x = 20;

        // Control registers
        panic_print_str(&x, &y, "--- Control Registers ---", INFO_TEXT_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT; x = 20;
        uint64_t cr0, cr2, cr3, cr4;
        __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4));
        ksprintf(info_buf, "CR0=%016lx  CR2=%016lx", cr0, cr2);
        panic_print_str(&x, &y, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT; x = 20;
        ksprintf(info_buf, "CR3=%016lx  CR4=%016lx", cr3, cr4);
        panic_print_str(&x, &y, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT * 2; x = 20;
        fb_flush(); // CP11

        // Stack dump
        panic_print_str(&x, &y, "--- Stack Dump (RSP-8 .. RSP+64) ---", INFO_TEXT_COLOR, PANIC_BG_COLOR);
        y += FONT_HEIGHT; x = 20;
        uint64_t *sp = (uint64_t *)(regs->rsp - 8);
        for (int i = 0; i < 9; i++) {
            if ((uint64_t)(sp + i) < 0xffff800000000000ULL) break;
            ksprintf(info_buf, "%016lx: %016lx", (uint64_t)(sp + i), sp[i]);
            panic_print_str(&x, &y, info_buf, PANIC_FG_COLOR, PANIC_BG_COLOR);
            y += FONT_HEIGHT; x = 20;
        }
        y += FONT_HEIGHT;
        fb_flush(); // CP12

        print_stack_trace(&x, &y, regs->rbp, PANIC_FG_COLOR, PANIC_BG_COLOR);
        fb_flush(); // CP13

    } else {
        panic_print_str(&x, &y, "No register state available.", PANIC_FG_COLOR, PANIC_BG_COLOR);
    }

    int bx = (int)fb_info->width - 36 * FONT_WIDTH;
    int by = (int)fb_info->height - 3 * FONT_HEIGHT;
    panic_print_str(&bx, &by, "SUKA EBAL KERNEL PANICI", INFO_TEXT_COLOR, PANIC_BG_COLOR);
    by += FONT_HEIGHT; bx = (int)fb_info->width - 36 * FONT_WIDTH;
    panic_print_str(&bx, &by, "SUKAAAAAAAAAAAAAAAAAAAAAAAAAA", INFO_TEXT_COLOR, PANIC_BG_COLOR);

    if (epstein) {
        int img_x = (int)fb_info->width - IMAGE_WIDTH - 8;
        int img_y = ((int)fb_info->height - IMAGE_HEIGHT) / 2;
        fb_draw_bitmap(img_x, img_y, sample_image_pixels, IMAGE_WIDTH, IMAGE_HEIGHT);
    }

    fb_flush();
    for (;;) __asm__ volatile("cli; hlt");
}