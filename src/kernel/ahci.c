#include "ahci.h"
#include "log.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "kstring.h"
#include "devfs.h"
#include "vfs.h"
#include "ide.h" // For ide_read_blocks_t / ide_write_blocks_t ioctl interface

#define HBA_PxCMD_ST    0x0001
#define HBA_PxCMD_FRE   0x0010
#define HBA_PxCMD_FR    0x4000
#define HBA_PxCMD_CR    0x8000

#define ATA_CMD_READ_DMA_EX     0x25
#define ATA_CMD_WRITE_DMA_EX    0x35

static hba_mem_t *g_hba = NULL;
static int g_primary_port = -1;
static hba_cmd_header_t *g_cmd_headers[32];
static hba_cmd_tbl_t *g_cmd_tbls[32];

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

static void port_stop_cmd(hba_port_t *port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    for (int timeout = 0; timeout < 10000; timeout++) {
        if (!(port->cmd & (HBA_PxCMD_FR | HBA_PxCMD_CR))) break;
        __asm__ volatile("pause");
    }
}

static void port_start_cmd(hba_port_t *port) {
    while (port->cmd & HBA_PxCMD_CR) { __asm__ volatile("pause"); }
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static void port_configure(hba_port_t *port, int port_no) {
    port_stop_cmd(port);

    void *clb_phys = pmm_alloc_page();
    void *clb_virt = vmm_phys_to_virt(clb_phys);
    memset(clb_virt, 0, PAGE_SIZE);

    port->clb = (uint32_t)(uint64_t)clb_phys;
    port->clbu = (uint32_t)((uint64_t)clb_phys >> 32);
    g_cmd_headers[port_no] = (hba_cmd_header_t *)clb_virt;

    void *fb_phys = pmm_alloc_page();
    void *fb_virt = vmm_phys_to_virt(fb_phys);
    memset(fb_virt, 0, PAGE_SIZE);

    port->fb = (uint32_t)(uint64_t)fb_phys;
    port->fbu = (uint32_t)((uint64_t)fb_phys >> 32);

    void *ctbl_phys = pmm_alloc_page();
    void *ctbl_virt = vmm_phys_to_virt(ctbl_phys);
    memset(ctbl_virt, 0, PAGE_SIZE);
    g_cmd_tbls[port_no] = (hba_cmd_tbl_t *)ctbl_virt;

    hba_cmd_header_t *cmd_hdr = &g_cmd_headers[port_no][0];
    cmd_hdr->ctba = (uint32_t)(uint64_t)ctbl_phys;
    cmd_hdr->ctbau = (uint32_t)((uint64_t)ctbl_phys >> 32);

    port->serr = (uint32_t)-1;
    port->is = (uint32_t)-1;

    port_start_cmd(port);
}

int ahci_read_sectors(uint8_t port_no, uint64_t lba, uint32_t count, void *buf) {
    if (!g_hba || port_no >= 32) return -1;
    hba_port_t *port = &g_hba->ports[port_no];

    port->is = (uint32_t)-1;
    int slot = 0;

    hba_cmd_header_t *cmd_hdr = &g_cmd_headers[port_no][slot];
    cmd_hdr->cfl = sizeof(uint32_t) * 5; // 5 dwords = 20 bytes FIS
    cmd_hdr->w = 0; // Read
    cmd_hdr->prdtl = 1;

    hba_cmd_tbl_t *cmd_tbl = g_cmd_tbls[port_no];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t));

    void *phys_buf = vmm_virt_to_phys(buf);
    cmd_tbl->prdt_entry[0].dba = (uint32_t)(uint64_t)phys_buf;
    cmd_tbl->prdt_entry[0].dbau = (uint32_t)((uint64_t)phys_buf >> 32);
    cmd_tbl->prdt_entry[0].dbc = (count * 512) - 1;
    cmd_tbl->prdt_entry[0].i = 1;

    uint8_t *fis = cmd_tbl->cfis;
    fis[0] = FIS_TYPE_REG_H2D;
    fis[1] = 0x80; // Command flag
    fis[2] = ATA_CMD_READ_DMA_EX;

    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40; // LBA mode

    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);

    fis[12] = (uint8_t)count;
    fis[13] = (uint8_t)(count >> 8);

    port->ci = 1 << slot;

    uint32_t timeout = 0;
    while (port->ci & (1 << slot)) {
        if (++timeout > 1000000) {
            klog(LOG_ERROR, "AHCI: Read timeout on port %d LBA %llu", port_no, lba);
            return -1;
        }
        __asm__ volatile("pause");
    }

    if (port->is & (1 << 30)) { // Task file error
        klog(LOG_ERROR, "AHCI: Disk read error on port %d", port_no);
        return -1;
    }
    return 0;
}

int ahci_write_sectors(uint8_t port_no, uint64_t lba, uint32_t count, const void *buf) {
    if (!g_hba || port_no >= 32) return -1;
    hba_port_t *port = &g_hba->ports[port_no];

    port->is = (uint32_t)-1;
    int slot = 0;

    hba_cmd_header_t *cmd_hdr = &g_cmd_headers[port_no][slot];
    cmd_hdr->cfl = sizeof(uint32_t) * 5;
    cmd_hdr->w = 1; // Write
    cmd_hdr->prdtl = 1;

    hba_cmd_tbl_t *cmd_tbl = g_cmd_tbls[port_no];
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t));

    void *phys_buf = vmm_virt_to_phys((void*)buf);
    cmd_tbl->prdt_entry[0].dba = (uint32_t)(uint64_t)phys_buf;
    cmd_tbl->prdt_entry[0].dbau = (uint32_t)((uint64_t)phys_buf >> 32);
    cmd_tbl->prdt_entry[0].dbc = (count * 512) - 1;
    cmd_tbl->prdt_entry[0].i = 1;

    uint8_t *fis = cmd_tbl->cfis;
    fis[0] = FIS_TYPE_REG_H2D;
    fis[1] = 0x80;
    fis[2] = ATA_CMD_WRITE_DMA_EX;

    fis[4] = (uint8_t)lba;
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40;

    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);

    fis[12] = (uint8_t)count;
    fis[13] = (uint8_t)(count >> 8);

    port->ci = 1 << slot;

    uint32_t timeout = 0;
    while (port->ci & (1 << slot)) {
        if (++timeout > 1000000) {
            klog(LOG_ERROR, "AHCI: Write timeout on port %d LBA %llu", port_no, lba);
            return -1;
        }
        __asm__ volatile("pause");
    }

    if (port->is & (1 << 30)) {
        klog(LOG_ERROR, "AHCI: Disk write error on port %d", port_no);
        return -1;
    }
    return 0;
}

static int ahci_ioctl(vfs_node_t *node, int request, void *arg) {
    (void)node;
    if (g_primary_port < 0) return -1;

    if (request == IDE_IOCTL_READ_BLOCKS) {
        ide_read_blocks_t *rb = (ide_read_blocks_t *)arg;
        return ahci_read_sectors((uint8_t)g_primary_port, rb->lba, rb->count, rb->buffer);
    } else if (request == IDE_IOCTL_WRITE_BLOCKS) {
        ide_write_blocks_t *wb = (ide_write_blocks_t *)arg;
        return ahci_write_sectors((uint8_t)g_primary_port, wb->lba, wb->count, wb->buffer);
    }
    return -1;
}

void ahci_init(pci_device_t *dev) {
    klog(LOG_INFO, "AHCI: Initializing SATA controller via PCI DMA...");

    // BAR 5 holds ABAR
    uint64_t abar_phys = dev->bars[5].base;
    if (!abar_phys) {
        klog(LOG_ERROR, "AHCI: Missing BAR5 ABAR.");
        return;
    }

    g_hba = (hba_mem_t *)vmm_phys_to_virt((void *)abar_phys);
    g_hba->ghc |= (1U << 31); // Enable AHCI

    uint32_t pi = g_hba->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            int type = check_type(&g_hba->ports[i]);
            if (type == 1) { // SATA drive
                klog(LOG_INFO, "AHCI: SATA drive discovered on port %d", i);
                port_configure(&g_hba->ports[i], i);
                if (g_primary_port < 0) g_primary_port = i;
            }
        }
    }

    if (g_primary_port >= 0) {
        vfs_node_t *sda = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
        if (sda) {
            memset(sda, 0, sizeof(vfs_node_t));
            strncpy(sda->name, "sda", sizeof(sda->name) - 1);
            sda->flags = VFS_BLOCKDEVICE | VFS_CHARDEVICE;
            sda->inode = vfs_get_next_inode();
            sda->ioctl = ahci_ioctl;
            sda->mode = 0660;
            devfs_register_device("sda", sda);
            klog(LOG_INFO, "AHCI: Registered primary SATA drive as /dev/sda");
        }
    }
}
