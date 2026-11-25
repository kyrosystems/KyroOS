#include "ide.h"
#include "deviceman.h"
#include "pci.h"
#include "log.h"
#include "heap.h"
#include "port_io.h"
#include "kstring.h"
#include "vfs.h"   // For vfs_node_t, vfs_root, vfs_finddir, vfs_mkdir
#include "devfs.h" // For devfs_register_device
#include "kyrofs.h" // For kyrofs_create_node
#include "fs_disk.h" // For fs_format
#include <stdbool.h>
#include <string.h>

static int ide_wait_for_ready(void);
static int ide_wait_for_drq(void);
static int ide_ioctl(vfs_node_t *node, int request, void* argp); // Forward declaration
static uint32_t ide_write_sectors_mode(uint8_t drive, uint32_t lba, uint8_t num_sectors, const uint16_t* buffer, bool flush_cache);
static int ide_flush_cache(void);

static uint64_t g_primary_disk_bytes = 0;

uint64_t ide_get_primary_capacity(void) {
    return g_primary_disk_bytes ? g_primary_disk_bytes : (4096ULL * 1024ULL * 1024ULL);
}

static uint32_t ide_transfer_bytes(uint8_t drive, uint64_t abs_byte_offset, uint32_t size, uint8_t *buffer, bool is_write) {
    if (size == 0) return 0;
    if (!buffer) return (uint32_t)-1;

    uint32_t bytes_transferred = 0;
    uint16_t sector_buf[256];

    while (bytes_transferred < size) {
        uint64_t cur_offset = abs_byte_offset + bytes_transferred;
        uint32_t cur_lba = (uint32_t)(cur_offset / 512);
        uint32_t offset_in_sector = (uint32_t)(cur_offset % 512);
        uint32_t bytes_remaining = size - bytes_transferred;

        if (offset_in_sector == 0 && bytes_remaining >= 512 && ((uintptr_t)(buffer + bytes_transferred) % 2 == 0)) {
            uint32_t sectors_to_do = bytes_remaining / 512;
            if (sectors_to_do > 128) sectors_to_do = 128;

            if (is_write) {
                if (ide_write_sectors_mode(drive, cur_lba, (uint8_t)sectors_to_do, (const uint16_t*)(buffer + bytes_transferred), false) != 0) {
                    return bytes_transferred > 0 ? bytes_transferred : (uint32_t)-1;
                }
            } else {
                if (ide_read_sectors(drive, cur_lba, (uint8_t)sectors_to_do, (uint16_t*)(buffer + bytes_transferred)) != 0) {
                    return bytes_transferred > 0 ? bytes_transferred : (uint32_t)-1;
                }
            }
            bytes_transferred += sectors_to_do * 512;
        } else {
            uint32_t chunk = 512 - offset_in_sector;
            if (chunk > bytes_remaining) chunk = bytes_remaining;

            if (is_write) {
                if (offset_in_sector != 0 || chunk < 512) {
                    if (ide_read_sectors(drive, cur_lba, 1, sector_buf) != 0) {
                        return bytes_transferred > 0 ? bytes_transferred : (uint32_t)-1;
                    }
                }
                memcpy((uint8_t*)sector_buf + offset_in_sector, buffer + bytes_transferred, chunk);
                if (ide_write_sectors_mode(drive, cur_lba, 1, sector_buf, false) != 0) {
                    return bytes_transferred > 0 ? bytes_transferred : (uint32_t)-1;
                }
            } else {
                if (ide_read_sectors(drive, cur_lba, 1, sector_buf) != 0) {
                    return bytes_transferred > 0 ? bytes_transferred : (uint32_t)-1;
                }
                memcpy(buffer + bytes_transferred, (uint8_t*)sector_buf + offset_in_sector, chunk);
            }
            bytes_transferred += chunk;
        }
    }

    if (is_write && bytes_transferred > 0) {
        if (ide_flush_cache() != 0) {
            return (uint32_t)-1;
        }
    }

    return bytes_transferred;
}

// Wrapper for ide_read_sectors to match read_vfs_t signature for raw disk access
static uint32_t ide_read_wrapper(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    uint8_t drive = (uint8_t)(uintptr_t)node->ptr;
    return ide_transfer_bytes(drive, offset, size, buffer, false);
}

// Wrapper for ide_write_sectors to match write_vfs_t signature for raw disk access
static uint32_t ide_write_wrapper(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    uint8_t drive = (uint8_t)(uintptr_t)node->ptr;
    return ide_transfer_bytes(drive, offset, size, buffer, true);
}

// New: Read and write sectors for a specific partition
static uint32_t ide_partition_read_sectors(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer);
static uint32_t ide_partition_write_sectors(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer);
static int ide_partition_ioctl(vfs_node_t *node, int request, void* argp);

// Structure to hold data specific to a partition VFS node
typedef struct {
    vfs_node_t *disk_node;      // Pointer to the parent disk device node (/dev/sda)
    uint32_t partition_lba_start; // Starting LBA of this partition
    uint32_t partition_size;    // Size of this partition in sectors
    uint8_t drive_id;           // The physical drive ID (e.g., 0 for primary master)
} ide_partition_data_t;

// Helper to create and register a partition node
static void ide_create_partition_node(vfs_node_t *disk_node, uint8_t partition_num,
                                      uint32_t lba_start, uint32_t size, uint8_t drive_id) {
    char name[MAX_FILENAME_LEN];
    ksprintf(name, "sda%u", partition_num);

    vfs_node_t *part_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!part_node) {
        klog(LOG_ERROR, "IDE: Failed to allocate partition node for /dev/%s.", name);
        return;
    }
    memset(part_node, 0, sizeof(vfs_node_t));
    strncpy(part_node->name, name, MAX_FILENAME_LEN - 1);

    ide_partition_data_t *part_data = (ide_partition_data_t*)kmalloc(sizeof(ide_partition_data_t));
    if (!part_data) {
        klog(LOG_ERROR, "IDE: Failed to allocate partition data for /dev/%s.", name);
        kfree(part_node);
        return;
    }
    part_data->disk_node = disk_node;
    part_data->partition_lba_start = lba_start;
    part_data->partition_size = size;
    part_data->drive_id = drive_id;

    part_node->read = ide_partition_read_sectors;
    part_node->write = ide_partition_write_sectors;
    part_node->ioctl = ide_partition_ioctl;
    part_node->ptr = part_data;
    part_node->flags = VFS_BLOCKDEVICE | VFS_CHARDEVICE;
    part_node->inode = vfs_get_next_inode();
    part_node->length = size * 512;
    part_node->mode = 0660;

    devfs_register_device(name, part_node);
    klog(LOG_INFO, "IDE: /dev/%s partition registered. LBA: %u, Size: %u sectors (%u MB).",
         name, lba_start, size, (size * 512) / (1024 * 1024));
}

// Function to read MBR and create partition nodes
static void ide_init_partitions(vfs_node_t *sda_node) {
    uint16_t* buffer = (uint16_t*)kmalloc(512);
    if (!buffer) {
        klog(LOG_ERROR, "IDE: Failed to allocate buffer for MBR.");
        return;
    }

    if (sda_node->read(sda_node, 0, 512, (uint8_t*)buffer) != 512) {
        klog(LOG_INFO, "IDE: Could not read MBR from /dev/sda, registering default partition.");
        ide_create_partition_node(sda_node, 1, 2048, (100 * 1024 * 1024) / 512, 0);
        kfree(buffer);
        return;
    }

    MBR* mbr = (MBR*)buffer;
    if (mbr->boot_signature != 0xAA55) {
        klog(LOG_WARN, "IDE: /dev/sda has raw disk or non-MBR format. Registering default partition /dev/sda1.");
        ide_create_partition_node(sda_node, 1, 2048, (100 * 1024 * 1024) / 512, 0);
        kfree(buffer);
        return;
    }

    int found = 0;
    for (int i = 0; i < 4; i++) {
        MBR_PartitionEntry *part = &mbr->partitions[i];
        if (part->type == 0x00 || part->sector_count == 0) continue;
        ide_create_partition_node(sda_node, i + 1, part->lba_first_sector, part->sector_count, (uintptr_t)sda_node->ptr);
        found++;
    }
    if (found == 0) {
        ide_create_partition_node(sda_node, 1, 2048, (100 * 1024 * 1024) / 512, 0);
    }
    kfree(buffer);
}

// Partition-specific read/write/ioctl implementations
static uint32_t ide_partition_read_sectors(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    ide_partition_data_t *part_data = (ide_partition_data_t*)node->ptr;
    if (!part_data) {
        klog(LOG_ERROR, "IDE_PART: Invalid partition data for node %s.", node->name);
        return (uint32_t)-1;
    }

    uint64_t part_bytes = (uint64_t)part_data->partition_size * 512ULL;
    if (offset >= part_bytes) return 0;
    if (offset + size > part_bytes) size = (uint32_t)(part_bytes - offset);

    uint64_t abs_offset = ((uint64_t)part_data->partition_lba_start * 512ULL) + offset;
    return ide_transfer_bytes(part_data->drive_id, abs_offset, size, buffer, false);
}

static uint32_t ide_partition_write_sectors(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    ide_partition_data_t *part_data = (ide_partition_data_t*)node->ptr;
    if (!part_data) {
        klog(LOG_ERROR, "IDE_PART: Invalid partition data for node %s.", node->name);
        return (uint32_t)-1;
    }

    uint64_t part_bytes = (uint64_t)part_data->partition_size * 512ULL;
    if (offset >= part_bytes) return 0;
    if (offset + size > part_bytes) size = (uint32_t)(part_bytes - offset);

    uint64_t abs_offset = ((uint64_t)part_data->partition_lba_start * 512ULL) + offset;
    return ide_transfer_bytes(part_data->drive_id, abs_offset, size, buffer, true);
}

static int ide_partition_ioctl(vfs_node_t *node, int request, void* argp) {
    if (!node || !argp) return -1;
    ide_partition_data_t *part_data = (ide_partition_data_t*)node->ptr;
    if (!part_data) {
        klog(LOG_ERROR, "IDE_PART: Invalid partition data for node %s.", node->name);
        return -1;
    }

    if (request == IDE_IOCTL_GET_DISK_INFO) {
        ide_disk_info_t *info = (ide_disk_info_t *)argp;
        info->total_sectors = part_data->partition_size;
        info->bytes_per_sector = 512;
        return 0;
    } else if (request == IDE_IOCTL_READ_BLOCKS) {
        ide_read_blocks_t *rb = (ide_read_blocks_t *)argp;
        if (!rb->buffer) return -1;
        if (rb->count == 0) return 0;
        // LBA passed to IOCTL is relative to partition
        if ((uint64_t)rb->lba + rb->count > part_data->partition_size) {
            klog(LOG_ERROR, "IDE_PART: IOCTL READ_BLOCKS for %s goes beyond partition boundary.", node->name);
            return -1;
        }
        uint32_t absolute_lba = part_data->partition_lba_start + rb->lba;
        return ide_read_sectors(part_data->drive_id, absolute_lba, rb->count, (uint16_t*)rb->buffer);
    } else if (request == IDE_IOCTL_WRITE_BLOCKS) {
        ide_write_blocks_t *wb = (ide_write_blocks_t *)argp;
        if (!wb->buffer) return -1;
        if (wb->count == 0) return 0;
        // LBA passed to IOCTL is relative to partition
        if ((uint64_t)wb->lba + wb->count > part_data->partition_size) {
            klog(LOG_ERROR, "IDE_PART: IOCTL WRITE_BLOCKS for %s goes beyond partition boundary.", node->name);
            return -1;
        }
        uint32_t absolute_lba = part_data->partition_lba_start + wb->lba;
        return ide_write_sectors(part_data->drive_id, absolute_lba, wb->count, (const uint16_t*)wb->buffer);
    } else if (request == IDE_IOCTL_FORMAT) {
        ide_ioctl_format_t *fmt_req = (ide_ioctl_format_t *)argp;
        // The LBA here is relative to the partition, and fs_format will add it to partition_lba_start
        return fs_format(node, part_data->partition_lba_start + fmt_req->partition_lba, fmt_req->partition_size);
    }
    klog(LOG_WARN, "IDE_PART: Unsupported IOCTL request %x for node %s", request, node->name);
    return -1;
}



// ATA Register Ports (Primary Bus)
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECCOUNT    0x1F2
#define ATA_PRIMARY_LBA_LO      0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HI      0x1F5
#define ATA_PRIMARY_DRIVE_HEAD  0x1F6
#define ATA_PRIMARY_COMMAND     0x1F7
#define ATA_PRIMARY_STATUS      0x1F7

// Status Register Bits
#define ATA_SR_BSY  0x80 // Busy
#define ATA_SR_DRDY 0x40 // Drive Ready
#define ATA_SR_DF   0x20 // Drive Fault
#define ATA_SR_DRQ  0x08 // Data Request
#define ATA_SR_ERR  0x01 // Error

// Commands
#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_CACHE_FLUSH 0xE7

static bool primary_master_present = false;

// Returns 0 on success, -1 on timeout
static int ide_wait_for_ready() {
    for(int i = 0; i < 100000; i++) {
        if (!(inb(ATA_PRIMARY_STATUS) & ATA_SR_BSY)) {
            return 0; // Success
        }
    }
    return -1; // Timeout
}

// Wait until BSY clears and DRQ sets (or ERR/DF sets). Returns 0 on DRQ, -1 on error/timeout.
static int ide_wait_for_drq(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t st = inb(ATA_PRIMARY_STATUS);
        if (st & ATA_SR_BSY) continue;
        if (st & (ATA_SR_DF | ATA_SR_ERR)) return -1;
        if (st & ATA_SR_DRQ) return 0;
    }
    return -1;
}

static int ide_probe(device_t* dev);
static int ide_attach(device_t* dev);

static driver_t ide_driver = {
    .name = "IDE Controller Driver",
    .probe = ide_probe,
    .attach = ide_attach,
    .next = NULL
};

static int ide_probe(device_t* dev) {
    if (dev->vendor_id == 0) return 0;
    uint8_t class_code = pci_get_class(dev->bus, dev->device, dev->func);
    uint8_t subclass_code = pci_get_subclass(dev->bus, dev->device, dev->func);
    if (class_code == 0x01 && subclass_code == 0x01) {
        klog(LOG_INFO, "IDE: Found an IDE controller.");
        return 1;
    }
    return 0;
}

static int ide_attach(device_t* dev) {
    klog(LOG_INFO, "IDE: Attaching to IDE controller.");
    if (dev) dev->driver = &ide_driver;
    primary_master_present = false;

    // Select master drive
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xA0); 
    
    // Send IDENTIFY
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);

    // Check if drive exists
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0 || status == 0xFF) {
        klog(LOG_INFO, "IDE: Primary master drive does not exist.");
        return 0;
    }

    if (ide_wait_for_ready() != 0) {
        klog(LOG_INFO, "IDE: Timed out waiting for IDENTIFY.");
        return 0;
    }

    // Check for ATAPI: mid=0x14, hi=0xEB or SATA: mid=0x69, hi=0x96
    uint8_t mid = inb(ATA_PRIMARY_LBA_MID);
    uint8_t hi = inb(ATA_PRIMARY_LBA_HI);
    if ((mid == 0x14 && hi == 0xEB) || (mid == 0x69 && hi == 0x96)) {
        klog(LOG_INFO, "IDE: ATAPI / SATA device detected (mid=0x%x, hi=0x%x), not ATA HDD.", mid, hi);
        return 0;
    }

    if (inb(ATA_PRIMARY_STATUS) & ATA_SR_ERR) {
        klog(LOG_INFO, "IDE: Primary master reported error on IDENTIFY.");
        return 0;
    }

    // Check for DRQ
    if (inb(ATA_PRIMARY_STATUS) & ATA_SR_DRQ) {
        klog(LOG_INFO, "IDE: Primary master drive found and ready.");
        primary_master_present = true;
            // Read IDENTIFY data
            uint16_t identify_data[256];
            insw(ATA_PRIMARY_DATA, identify_data, 256);

            uint32_t s28 = identify_data[60] | ((uint32_t)identify_data[61] << 16);
            if (identify_data[83] & (1 << 10)) {
                uint64_t s48 = (uint64_t)identify_data[100] |
                               ((uint64_t)identify_data[101] << 16) |
                               ((uint64_t)identify_data[102] << 32) |
                               ((uint64_t)identify_data[103] << 48);
                if (s48 > 0) g_primary_disk_bytes = s48 * 512ULL;
            }
            if (g_primary_disk_bytes == 0 && s28 > 0) {
                g_primary_disk_bytes = (uint64_t)s28 * 512ULL;
            }
            if (g_primary_disk_bytes == 0) {
                g_primary_disk_bytes = 4096ULL * 1024ULL * 1024ULL;
            }
            klog(LOG_INFO, "IDE: Detected capacity: %u MB", (uint32_t)(g_primary_disk_bytes / (1024 * 1024)));
        
            // Create device node for /dev/sda
            vfs_node_t *sda_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
            if (!sda_node) {
                klog(LOG_ERROR, "IDE: Failed to allocate /dev/sda node.");
                return -1;
            }
            memset(sda_node, 0, sizeof(vfs_node_t));
            strncpy(sda_node->name, "sda", MAX_FILENAME_LEN - 1);
            sda_node->read = ide_read_wrapper;
            sda_node->write = ide_write_wrapper;
            sda_node->ioctl = ide_ioctl;
            sda_node->ptr = (void*)(uintptr_t)0; // drive 0
            sda_node->flags = VFS_BLOCKDEVICE | VFS_CHARDEVICE;
            sda_node->inode = vfs_get_next_inode();
            sda_node->length = 0xFFFFFFFFU;
            sda_node->mode = 0660;

            devfs_register_device("sda", sda_node);
            klog(LOG_INFO, "IDE: /dev/sda registered in DevFS.");

            // Now initialize partitions for this disk
            ide_init_partitions(sda_node);
            } else {
        klog(LOG_WARN, "IDE: Primary master drive not responding correctly to IDENTIFY.");
    }

    return 0;
}

// New IOCTL handler for IDE devices
static int ide_ioctl(vfs_node_t *node, int request, void* argp) {
    if (!argp) return -1;

    // The node->ptr could be used to store a drive ID if multiple IDE drives
    uint8_t drive = 0; 

    if (request == IDE_IOCTL_GET_DISK_INFO) {
        ide_disk_info_t *info = (ide_disk_info_t *)argp;
        info->total_sectors = g_primary_disk_bytes / 512;
        info->bytes_per_sector = 512;
        return 0;
    } else if (request == IDE_IOCTL_READ_BLOCKS) {
        ide_read_blocks_t *rb = (ide_read_blocks_t *)argp;
        if (!rb->buffer) return -1;
        if (rb->count == 0) return 0;
        return ide_read_sectors(drive, rb->lba, rb->count, (uint16_t*)rb->buffer);
    } else if (request == IDE_IOCTL_WRITE_BLOCKS) {
        ide_write_blocks_t *wb = (ide_write_blocks_t *)argp;
        if (!wb->buffer) return -1;
        if (wb->count == 0) return 0;
        return ide_write_sectors(drive, wb->lba, wb->count, (const uint16_t*)wb->buffer);
    } else if (request == IDE_IOCTL_FORMAT) {
        ide_ioctl_format_t *fmt_req = (ide_ioctl_format_t *)argp;
        return fs_format(node, fmt_req->partition_lba, fmt_req->partition_size);
    }
    klog(LOG_WARN, "IDE: Unsupported IOCTL request %x", request);
    return -1;
}

void ide_test_read() {
    if (!primary_master_present) {
        klog(LOG_ERROR, "IDE Test: Aborting, primary master not found.");
        return;
    }

    uint16_t* buffer = (uint16_t*)kmalloc(512);
    if (!buffer) {
        klog(LOG_ERROR, "IDE Test: Failed to allocate buffer for MBR.");
        return;
    }

    klog(LOG_INFO, "IDE Test: Reading MBR from drive 0...");
    if (ide_read_sectors(0, 0, 1, buffer) != 0) {
        klog(LOG_ERROR, "IDE Test: Failed to read MBR.");
        kfree(buffer);
        return;
    }

    MBR* mbr = (MBR*)buffer;
    if (mbr->boot_signature != 0xAA55) {
        klog(LOG_WARN, "IDE Test: Invalid MBR signature.");
        kfree(buffer);
        return;
    }

    klog(LOG_INFO, "IDE Test: MBR signature is valid. Partitions:");
    char print_buf[128];
    for (int i = 0; i < 4; i++) {
        if (mbr->partitions[i].type == 0) continue;
        ksprintf(print_buf, "  [%d] Type: 0x%x, Start LBA: %d, Size: %d sectors",
            i, (int)mbr->partitions[i].type, (int)mbr->partitions[i].lba_first_sector, (int)mbr->partitions[i].sector_count);
        klog(LOG_INFO, "%s", print_buf);
    }

    kfree(buffer);
}

uint32_t ide_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, uint16_t* buffer) {
    if (num_sectors == 0) return 0;
    if (!buffer || drive != 0 || !primary_master_present) return (uint32_t)-1;

    if (ide_wait_for_ready() != 0) return (uint32_t)-1;

    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, num_sectors);
    outb(ATA_PRIMARY_LBA_LO, lba & 0xFF);
    outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_PIO);

    for (int i = 0; i < num_sectors; i++) {
        for (int k = 0; k < 4; k++) inb(ATA_PRIMARY_STATUS); // 400ns delay
        if (ide_wait_for_drq() != 0) {
            uint8_t st = inb(ATA_PRIMARY_STATUS);
            klog(LOG_WARN, "IDE: Read error (status 0x%x).", st);
            return (uint32_t)-1;
        }
        insw(ATA_PRIMARY_DATA, buffer + (i * 256), 256);
    }
    return 0;
}

static int ide_flush_cache(void) {
    if (!primary_master_present) return -1;
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    return ide_wait_for_ready();
}

static uint32_t ide_write_sectors_mode(uint8_t drive, uint32_t lba, uint8_t num_sectors, const uint16_t* buffer, bool flush_cache) {
    if (num_sectors == 0) return 0;
    if (!buffer || drive != 0 || !primary_master_present) return (uint32_t)-1;
    
    if (ide_wait_for_ready() != 0) return (uint32_t)-1;

    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, num_sectors);
    outb(ATA_PRIMARY_LBA_LO, lba & 0xFF);
    outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_PIO);

    for (int i = 0; i < num_sectors; i++) {
        for (int k = 0; k < 4; k++) inb(ATA_PRIMARY_STATUS); // 400ns delay
        if (ide_wait_for_drq() != 0) {
            klog(LOG_ERROR, "IDE: Write error (status 0x%x).", inb(ATA_PRIMARY_STATUS));
            return (uint32_t)-1;
        }
        outsw(ATA_PRIMARY_DATA, buffer + (i * 256), 256);
    }
    
    if (flush_cache) {
        if (ide_flush_cache() != 0) return (uint32_t)-1;
    }
    
    return 0;
}

uint32_t ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, const uint16_t* buffer) {
    return ide_write_sectors_mode(drive, lba, num_sectors, buffer, true);
}

void ide_driver_init() {
    deviceman_register_driver(&ide_driver);
    klog(LOG_INFO, "IDE driver registered with Device Manager.");
}

#define SPARSE_RAMDISK_SECTORS (64 * 1024 * 1024 / 512) // 64 MB virtual disk
#define SPARSE_PAGES_COUNT     (SPARSE_RAMDISK_SECTORS / 8) // 4096 bytes per page = 8 sectors

static uint8_t *sparse_ramdisk_pages[SPARSE_PAGES_COUNT] = {0};

static uint32_t sparse_disk_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    uint64_t max_bytes = (uint64_t)SPARSE_RAMDISK_SECTORS * 512;
    if (offset >= max_bytes) return 0;
    if (offset + size > max_bytes) size = (uint32_t)(max_bytes - offset);

    uint32_t bytes_read = 0;
    while (bytes_read < size) {
        uint64_t cur_off = offset + bytes_read;
        uint32_t page_idx = (uint32_t)(cur_off / 4096);
        uint32_t page_off = (uint32_t)(cur_off % 4096);
        uint32_t chunk = 4096 - page_off;
        if (chunk > size - bytes_read) chunk = size - bytes_read;

        if (page_idx < SPARSE_PAGES_COUNT && sparse_ramdisk_pages[page_idx]) {
            memcpy(buffer + bytes_read, sparse_ramdisk_pages[page_idx] + page_off, chunk);
        } else {
            memset(buffer + bytes_read, 0, chunk);
        }
        bytes_read += chunk;
    }
    return bytes_read;
}

static uint32_t sparse_disk_write(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    uint64_t max_bytes = (uint64_t)SPARSE_RAMDISK_SECTORS * 512;
    if (offset >= max_bytes) return 0;
    if (offset + size > max_bytes) size = (uint32_t)(max_bytes - offset);

    uint32_t bytes_written = 0;
    while (bytes_written < size) {
        uint64_t cur_off = offset + bytes_written;
        uint32_t page_idx = (uint32_t)(cur_off / 4096);
        uint32_t page_off = (uint32_t)(cur_off % 4096);
        uint32_t chunk = 4096 - page_off;
        if (chunk > size - bytes_written) chunk = size - bytes_written;

        if (page_idx < SPARSE_PAGES_COUNT) {
            if (!sparse_ramdisk_pages[page_idx]) {
                sparse_ramdisk_pages[page_idx] = (uint8_t *)kmalloc(4096);
                if (sparse_ramdisk_pages[page_idx]) {
                    memset(sparse_ramdisk_pages[page_idx], 0, 4096);
                }
            }
            if (sparse_ramdisk_pages[page_idx]) {
                memcpy(sparse_ramdisk_pages[page_idx] + page_off, buffer + bytes_written, chunk);
            }
        }
        bytes_written += chunk;
    }
    return bytes_written;
}

static int sparse_disk_ioctl(vfs_node_t *node, int request, void* argp) {
    (void)node;
    if (!argp && request != IDE_IOCTL_FORMAT) return -1;
    if (request == IDE_IOCTL_GET_DISK_INFO) {
        ide_disk_info_t *info = (ide_disk_info_t *)argp;
        info->total_sectors = SPARSE_RAMDISK_SECTORS;
        info->bytes_per_sector = 512;
        return 0;
    } else if (request == IDE_IOCTL_READ_BLOCKS) {
        ide_read_blocks_t *rb = (ide_read_blocks_t *)argp;
        if (!rb->buffer) return -1;
        if (rb->count == 0) return 0;
        if ((uint64_t)rb->lba + rb->count > SPARSE_RAMDISK_SECTORS) return -1;
        if ((uint64_t)rb->count * 512 > 0xFFFFFFFFULL) return -1;
        sparse_disk_read(node, (uint64_t)rb->lba * 512, rb->count * 512, (uint8_t *)rb->buffer);
        return 0;
    } else if (request == IDE_IOCTL_WRITE_BLOCKS) {
        ide_write_blocks_t *wb = (ide_write_blocks_t *)argp;
        if (!wb->buffer) return -1;
        if (wb->count == 0) return 0;
        if ((uint64_t)wb->lba + wb->count > SPARSE_RAMDISK_SECTORS) return -1;
        if ((uint64_t)wb->count * 512 > 0xFFFFFFFFULL) return -1;
        sparse_disk_write(node, (uint64_t)wb->lba * 512, wb->count * 512, (uint8_t *)(uintptr_t)wb->buffer);
        return 0;
    } else if (request == IDE_IOCTL_FORMAT) {
        return 0;
    }
    return -1;
}

void ide_init(void) {
    ide_attach(NULL);
    if (!primary_master_present) {
        if (g_primary_disk_bytes == 0) g_primary_disk_bytes = (uint64_t)SPARSE_RAMDISK_SECTORS * 512;
        klog(LOG_INFO, "IDE: Initializing virtual storage device (/dev/sda).");
        vfs_node_t *sda_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
        if (sda_node) {
            memset(sda_node, 0, sizeof(vfs_node_t));
            strncpy(sda_node->name, "sda", MAX_FILENAME_LEN - 1);
            sda_node->read = sparse_disk_read;
            sda_node->write = sparse_disk_write;
            sda_node->ioctl = sparse_disk_ioctl;
            sda_node->flags = VFS_BLOCKDEVICE | VFS_CHARDEVICE;
            sda_node->inode = vfs_get_next_inode();
            sda_node->length = (uint64_t)SPARSE_RAMDISK_SECTORS * 512;
            sda_node->mode = 0660;
            devfs_register_device("sda", sda_node);

            vfs_node_t *sda1_node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
            if (sda1_node) {
                memset(sda1_node, 0, sizeof(vfs_node_t));
                strncpy(sda1_node->name, "sda1", MAX_FILENAME_LEN - 1);
                sda1_node->read = sparse_disk_read;
                sda1_node->write = sparse_disk_write;
                sda1_node->ioctl = sparse_disk_ioctl;
                sda1_node->flags = VFS_BLOCKDEVICE | VFS_CHARDEVICE;
                sda1_node->inode = vfs_get_next_inode();
                sda1_node->length = (uint64_t)SPARSE_RAMDISK_SECTORS * 512;
                sda1_node->mode = 0660;
                devfs_register_device("sda1", sda1_node);
            }
        }
    }
}