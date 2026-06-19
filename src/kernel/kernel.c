#include "kstring.h"
#include "limine.h"
#include <stddef.h>
#include <stdint.h>
#include "fb.h"
#include "gdt.h"
#include "idt.h"
#include "tss.h"
#include "pmm.h"
#include "heap.h"
#include "thread.h"
#include "log.h"
#include "vfs.h"
#include "pci.h"
#include "shell.h"
#include "scheduler.h"
#include "event.h"
#include "keyboard.h"
#include "mouse.h"
#include "rtc.h"
#include "isr.h"
#include "devfs.h"
#include "syscall.h"
#include "kyrofs.h"
#include "lkm.h"
#include "ip.h"
#include "arp.h"
#include "udp.h"
#include "tcp.h"
#include "dhcp.h"
#include "dns.h"

__attribute__((used, section(".limine_reqs"))) static volatile struct limine_hhdm_request hhdm_request = {.id = LIMINE_HHDM_REQUEST, .revision = 0};
__attribute__((used, section(".limine_reqs"))) static volatile struct limine_framebuffer_request framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0};
__attribute__((used, section(".limine_reqs"))) static volatile struct limine_memmap_request memmap_request = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};
__attribute__((used, section(".limine_reqs"))) static volatile struct limine_module_request module_request = {.id = LIMINE_MODULE_REQUEST, .revision = 0};

uint64_t kernel_hhdm_offset = 0;

void kmain_x64(void) {
    if (hhdm_request.response != NULL) kernel_hhdm_offset = hhdm_request.response->offset;
    // Initialize logging first to capture any early messages
    log_init();
    pmm_init(memmap_request.response, kernel_hhdm_offset);
    heap_init();
    // Initialize the framebuffer if available
    if (framebuffer_request.response != NULL && framebuffer_request.response->framebuffer_count > 0) {
        fb_init(framebuffer_request.response->framebuffers[0]);
        log_update_console_dimensions();
        fb_init_backbuffer();
    }
    // Initialize GDT, IDT, and TSS
    gdt_init();
    idt_init();
    tss_init();
    // Initialize event handling and timer
    event_init();
    timer_init(100);
    keyboard_init();
    mouse_init();
    rtc_init();
    // Initialize threading and virtual file system
    thread_init();
    vfs_init();
    kyrofs_init(module_request.response);
    devfs_init();
    vfs_mount(vfs_resolve_path(vfs_root, "/dev"), NULL, "devfs");
    syscall_init();
    // Initialize PCI and enumerate devices
    pci_enumerate();

    // network stack initialization
    ip_init();
    arp_init();
    udp_init();
    tcp_init();
    dhcp_init();
    dhcp_client_start();
    dns_init(0x08080808); // Google DNS server IP address in hex 
    // Initialize the loadable kernel module manager
    lkm_manager_init(&g_lkm_manager);
    // Load the kernel modules specified in the Limine module request
    klog(LOG_INFO, "KyroOS 26.03.12 Beryllium Ready.");
    // Start the shell in a new thread
    __asm__ __volatile__("sti");
    thread_create(shell_main, NULL);
    // Start the scheduler
    while (1) { schedule(); __asm__ __volatile__("hlt"); }
}
