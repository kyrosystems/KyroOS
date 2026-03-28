#include "ahci.h"
#include "log.h"
#include "vmm.h"
#include "heap.h"
#include "kstring.h"

static int check_type(hba_port_t *port) {
    uint32_t ssts = port->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != HBA_PORT_DET_PRESENT) return 0;
    if (ipm != HBA_PORT_IPM_ACTIVE) return 0;

    switch (port->sig) {
        case SATA_SIG_ATAPI: return 2;
        case SATA_SIG_SEMB:  return 3;
        case SATA_SIG_PM:    return 4;
        default:             return 1;
    }
}

void ahci_init(pci_device_t *dev) {
    klog(LOG_INFO, "AHCI: Initializing SATA controller...");

    // AHCI Base Address is in BAR 5
    hba_mem_t *hba = (hba_mem_t *)dev->bars[5].base;
    
    // Enable AHCI mode
    hba->ghc |= (1U << 31); 
    
    uint32_t pi = hba->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            int type = check_type(&hba->ports[i]);
            if (type == 1) {
                klog(LOG_INFO, "AHCI: SATA drive found on port %d", i);
            } else if (type == 2) {
                klog(LOG_INFO, "AHCI: SATAPI drive found on port %d", i);
            } else if (type == 3) {
                klog(LOG_INFO, "AHCI: SEMB drive found on port %d", i);
            } else if (type == 4) {
                klog(LOG_INFO, "AHCI: PM drive found on port %d", i);
            }
        }
    }
}
