#include "kstring.h"
#include "limine.h"
#include <stddef.h>
#include <stdint.h>

#include "arp.h"
#include "deviceman.h"
#include "dhcp.h"
#include "e1000.h"
#include "elf.h"
#include "event.h"
#include "fb.h"
#include "fs_disk_vfs.h" // For fs_disk_vfs_init()
#include "gdt.h"
#include "heap.h"
#include "ide.h"
#include "idt.h"
#include "image.h" // Include image.h
#include "ip.h"
#include "isr.h" // For timer_get_ticks()
#include "keyboard.h"
#include "kyrofs.h"
#include "log.h"
#include "mouse.h"
#include "net.h"
#include "panic_screen.h"
#include "pci.h"
#include "pmm.h"
#include "shell.h"
#include "syscall.h"
#include "tcp.h" // For tcp_init()
#include "thread.h"
#include "tss.h"
#include "tty.h"
#include "drivers/gpu/amd_gpu/radeon.h" // New include
#include "udp.h"
#include "version.h"
#include "vmm.h"


// Limine Requests with order guarantees
__attribute__((
    used,
    section(
        ".limine_reqs_start"))) static volatile LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_reqs"))) static volatile uint64_t
    limine_base_revision[3] = {0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 1};
__attribute__((
    used, section(".limine_reqs"))) static volatile struct limine_framebuffer_request
    framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0};
__attribute__((
    used, section(".limine_reqs"))) static volatile struct limine_memmap_request
    memmap_request = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};
__attribute__((
    used, section(".limine_reqs"))) static volatile struct limine_hhdm_request
    hhdm_request = {.id = LIMINE_HHDM_REQUEST, .revision = 0};
__attribute__((
    used,
    section(
        ".limine_reqs"))) static volatile struct limine_kernel_address_request
    kernel_address_request = {.id = LIMINE_KERNEL_ADDRESS_REQUEST,
                              .revision = 0};
uint64_t kernel_hhdm_offset = 0;
__attribute__((
    used, section(".limine_reqs"))) static volatile struct limine_module_request
    module_request = {.id = LIMINE_MODULE_REQUEST, .revision = 0};
__attribute__((
    used,
    section(".limine_reqs_end"))) static volatile LIMINE_REQUESTS_END_MARKER;

// Kernel entry point
void kmain_x64(void) {
  // --- FORCE PANIC FOR TESTING ---
  // __asm__ __volatile__("int $0x00"); // This will trigger a divide-by-zero
  // error

  serial_print("KMAIN: before log_init()\n");
  log_init(); // Serial only, VGA cleared by console_clear after HHDM set.
  serial_print("KMAIN: after log_init()\n");

  serial_print("KMAIN: before framebuffer_request check\n");
  if (framebuffer_request.response == NULL ||
      framebuffer_request.response->framebuffer_count < 1) {
    for (;;) {
      __asm__ __volatile__("hlt");
    } // System cannot boot without framebuffer
  }
  serial_print("KMAIN: after framebuffer_request check\n");

  struct limine_framebuffer *fb_tag =
      framebuffer_request.response->framebuffers[0];

  serial_print("KMAIN: before hhdm_request check\n");
  if (hhdm_request.response != NULL) {
    kernel_hhdm_offset = hhdm_request.response->offset;
  }
  serial_print("KMAIN: after hhdm_request check\n");

  serial_print("KMAIN: before fb_init()\n");
  fb_init(fb_tag);
  serial_print("KMAIN: after fb_init()\n");

  serial_print("KMAIN: before console_clear()\n");
  console_clear();
  serial_print("KMAIN: after console_clear()\n");

  serial_print("KMAIN: before memmap_request check\n");
  if (memmap_request.response != NULL) {
    serial_print("KMAIN: before pmm_init()\n");
    pmm_init(memmap_request.response, kernel_hhdm_offset);
    serial_print("KMAIN: after pmm_init()\n");

    serial_print("KMAIN: before heap_init()\n");
    heap_init();
    serial_print("KMAIN: after heap_init()\n");

    serial_print("KMAIN: before vmm_init()\n");
    vmm_init();
    serial_print("KMAIN: after vmm_init()\n");

    serial_print("KMAIN: before fb_init_backbuffer()\n");
    fb_init_backbuffer();
    serial_print("KMAIN: after fb_init_backbuffer()\n");

    serial_print("KMAIN: before thread_init()\n");
    thread_init(); // Basic thread system initialization
    serial_print("KMAIN: after thread_init()\n");
  }
  serial_print("KMAIN: after memmap_request check\n");

  serial_print("KMAIN: before gdt_init()\n");
  gdt_init();
  serial_print("KMAIN: after gdt_init()\n");

  serial_print("KMAIN: before idt_init()\n");
  idt_init();
  serial_print("KMAIN: after idt_init()\n");

  serial_print("KMAIN: before tss_init()\n");
  tss_init();
  serial_print("KMAIN: after tss_init()\n");

  serial_print("KMAIN: before keyboard_init()\n");
  keyboard_init();
  serial_print("KMAIN: after keyboard_init()\n");

  serial_print("KMAIN: before timer_init()\n");
  timer_init(100);
  serial_print("KMAIN: after timer_init()\n");

  serial_print("KMAIN: before vfs_init()\n");
  vfs_init();
  serial_print("KMAIN: after vfs_init()\n");

  serial_print("KMAIN: before kyrofs_init()\n");
  kyrofs_init();
  serial_print("KMAIN: after kyrofs_init()\n");

  serial_print("KMAIN: before tty_init()\n");
  tty_init();
  serial_print("KMAIN: after tty_init()\n");

  serial_print("KMAIN: before fs_disk_vfs_init()\n");
  fs_disk_vfs_init(); // Initialize and register the disk filesystem
  serial_print("KMAIN: after fs_disk_vfs_init()\n");

  serial_print("KMAIN: before panic_screen_init()\n");
  panic_screen_init(); // Initialize the panic screen system
  serial_print("KMAIN: after panic_screen_init()\n");

  serial_print("KMAIN: before module_request check\n");
  if (module_request.response != NULL) {
    klog(LOG_INFO, "Loading Boot Modules...");
    for (uint64_t i = 0; i < module_request.response->module_count; i++) {
      struct limine_file *module = module_request.response->modules[i];
      if (!module || !module->path)
        continue;
      // Path buffer, ensure it's large enough.
      char path[128]; // Use a reasonably sized buffer for path
      char *filename = (char *)module->path;
      for (char *p = (char *)module->path; *p; p++)
        if (*p == '/')
          filename = p + 1;
      ksprintf(path, "/bin/%s", filename);
      kyrofs_add_file(path, module->address, module->size);
    }
  }
  serial_print("KMAIN: after module_request check\n");

  serial_print("KMAIN: before deviceman_init()\n");
  deviceman_init();
  serial_print("KMAIN: after deviceman_init()\n");

  serial_print("KMAIN: before ide_driver_init()\n");
  ide_driver_init();
  serial_print("KMAIN: after ide_driver_init()\n");

  serial_print("KMAIN: before amdgpu_driver_init()\n"); // New
  amdgpu_driver_init();                                 // New
  serial_print("KMAIN: after amdgpu_driver_init()\n");  // New

  serial_print("KMAIN: before net_init()\n");
  net_init();
  serial_print("KMAIN: after net_init()\n");

  serial_print("KMAIN: before e1000_driver_init()\n");
  e1000_driver_init();
  serial_print("KMAIN: after e1000_driver_init()\n");

  serial_print("KMAIN: before ip_init()\n");
  ip_init();
  serial_print("KMAIN: after ip_init()\n");

  serial_print("KMAIN: before arp_init()\n");
  arp_init();
  serial_print("KMAIN: after arp_init()\n");

  serial_print("KMAIN: before udp_init()\n");
  udp_init();
  serial_print("KMAIN: after udp_init()\n");

  serial_print("KMAIN: before tcp_init()\n");
  tcp_init(); // Initialize TCP stack
  serial_print("KMAIN: after tcp_init()\n");

  serial_print("KMAIN: before dhcp_init()\n");
  dhcp_init();
  serial_print("KMAIN: after dhcp_init()\n");

  serial_print("KMAIN: before pci_check_all_buses()\n");
  pci_check_all_buses();
  serial_print("KMAIN: after pci_check_all_buses()\n");

  serial_print("KMAIN: before deviceman_probe_devices()\n");
  deviceman_probe_devices();
  serial_print("KMAIN: after deviceman_probe_devices()\n");

  serial_print("KMAIN: before dhcp_discover()\n");
  dhcp_client_start();
  serial_print("KMAIN: after dhcp_discover()\n");

  serial_print("KMAIN: before starting shell_main as a kernel thread\n");
  thread_create(shell_main, NULL);
  serial_print("KMAIN: after starting shell_main as a kernel thread\n");

  __asm__ __volatile__(
      "sti"); // Enable interrupts only if necessary services are started
  klog(LOG_INFO, "Interrupts Enabled.");
}

// fuck Rust
// Why everyone rewriting own projects to Rust?
// this is a dumb thing
// C is better
// C have real stability
// C is the best
// fuck Rust

// also i love python