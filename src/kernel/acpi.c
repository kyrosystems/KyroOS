#include "acpi.h"
#include "log.h"
#include "port_io.h"
#include "kstring.h"
#include "limine.h"

__attribute__((used, section(".limine_reqs"))) static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

static fadt_t *fadt = NULL;

void acpi_init() {
    if (rsdp_request.response == NULL || rsdp_request.response->address == NULL) {
        klog(LOG_ERROR, "ACPI: RSDP not found.");
        return;
    }

    rsdp_t *rsdp = (rsdp_t *)rsdp_request.response->address;
    acpi_sdt_header_t *rsdt = (acpi_sdt_header_t *)(uint64_t)rsdp->rsdt_address;

    uint32_t *entries = (uint32_t *)((uint64_t)rsdt + sizeof(acpi_sdt_header_t));
    uint32_t num_entries = (rsdt->length - sizeof(acpi_sdt_header_t)) / 4;

    for (uint32_t i = 0; i < num_entries; i++) {
        acpi_sdt_header_t *h = (acpi_sdt_header_t *)(uint64_t)entries[i];
        if (memcmp(h->signature, "FACP", 4) == 0) {
            fadt = (fadt_t *)h;
            klog(LOG_INFO, "ACPI: Found FADT. Reset register ready.");
            break;
        }
    }
}

void acpi_reboot() {
    if (fadt && (fadt->flags & (1 << 10))) { // Check bit 10: Reset Register supported
        klog(LOG_INFO, "ACPI: Attempting hardware reset...");
        // Assuming I/O space for now as it's most common for reset_reg
        uint32_t port = *(uint32_t *)(&fadt->reset_reg_addr[4]); // Address field in GAS
        outb(port, fadt->reset_value);
    }
    
    // Fallback to keyboard controller
    klog(LOG_WARN, "ACPI: Reset failed or not supported. Falling back to 8042.");
    outb(0x64, 0xFE);
}
