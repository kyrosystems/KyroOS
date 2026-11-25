#include "smp.h"
#include "log.h"
#include "gdt.h"
#include "idt.h"
#include "scheduler.h"
#include "spinlock.h"

__attribute__((used, section(".limine_reqs")))
static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};

static uint32_t active_cpu_count = 1;
static spinlock_t smp_lock;

static void smp_ap_entry(struct limine_smp_info *info) {
    (void)info;
    __asm__ volatile("cli");

    // Reload GDT on AP core
    gdt_load_ap();

    // Reload IDT on AP core (load IDTR only, do NOT reinitialize IDT or touch PIC!)
    idt_load_ap();

    spinlock_acquire(&smp_lock);
    active_cpu_count++;
    spinlock_release(&smp_lock);

    // Keep secondary core cleanly parked with interrupts disabled
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

void smp_init(void) {
    spinlock_init(&smp_lock);

    if (!smp_request.response) {
        klog(LOG_INFO, "SMP: Limine SMP response unavailable. Running in single-core mode.");
        active_cpu_count = 1;
        return;
    }

    struct limine_smp_response *resp = smp_request.response;
    klog(LOG_INFO, "SMP: Found %llu cores (BSP LAPIC ID: %u)", resp->cpu_count, resp->bsp_lapic_id);

    for (uint64_t i = 0; i < resp->cpu_count; i++) {
        struct limine_smp_info *cpu = resp->cpus[i];
        if (cpu->lapic_id != resp->bsp_lapic_id) {
            cpu->goto_address = smp_ap_entry;
        }
    }

    // Give AP cores a brief spin to register
    for (volatile int i = 0; i < 50000; i++) {
        __asm__ volatile("pause");
    }

    klog(LOG_INFO, "SMP: %u active cores online.", active_cpu_count);
}

uint32_t smp_get_cpu_count(void) {
    return active_cpu_count;
}

uint32_t smp_get_current_cpu_id(void) {
    uint32_t ebx = 0;
    __asm__ volatile("cpuid" : "=b"(ebx) : "a"(1) : "ecx", "edx");
    return (ebx >> 24) & 0xFF;
}
