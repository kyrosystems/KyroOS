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

__attribute__((used, section(".limine_reqs"))) static volatile struct limine_hhdm_request hhdm_request = {.id = LIMINE_HHDM_REQUEST, .revision = 0};
__attribute__((used, section(".limine_reqs"))) static volatile struct limine_framebuffer_request framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0};
__attribute__((used, section(".limine_reqs"))) static volatile struct limine_memmap_request memmap_request = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};
__attribute__((used, section(".limine_reqs"))) static volatile struct limine_module_request module_request = {.id = LIMINE_MODULE_REQUEST, .revision = 0};

uint64_t kernel_hhdm_offset = 0;

void kmain_x64(void) {
    if (hhdm_request.response != NULL) kernel_hhdm_offset = hhdm_request.response->offset;
    
    log_init();
    pmm_init(memmap_request.response, kernel_hhdm_offset);
    heap_init();
    
    if (framebuffer_request.response != NULL && framebuffer_request.response->framebuffer_count > 0) {
        fb_init(framebuffer_request.response->framebuffers[0]);
        log_update_console_dimensions();
        fb_init_backbuffer();
    }

    gdt_init();
    idt_init();
    tss_init();
    
    event_init();
    timer_init(100);
    keyboard_init();
    mouse_init();
    rtc_init();

    thread_init();
    vfs_init();
    kyrofs_init(module_request.response);
    devfs_init();
    vfs_mount(vfs_resolve_path(vfs_root, "/dev"), NULL, "devfs");
    syscall_init();

    pci_enumerate();

    lkm_manager_init(&g_lkm_manager);

    klog(LOG_INFO, "KyroOS 26.03.11 Titanium Ready.");
    
    __asm__ __volatile__("sti");
    thread_create(shell_main, NULL);
    
    while (1) { schedule(); __asm__ __volatile__("hlt"); }
}
