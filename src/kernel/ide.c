#include "ide.h"
#include "deviceman.h"
#include "pci.h"
#include "log.h"
#include "heap.h"
#include "port_io.h"
#include "kstring.h"
#include "vfs.h"   // For vfs_node_t, vfs_root, vfs_finddir, vfs_mkdir
#include "kyrofs.h" // For kyrofs_create_node
#include "fs_disk.h" // For fs_format
#include <stdbool.h>
#include <string.h>

static int ide_ioctl(vfs_node_t *node, int request, void* argp); // Forward declaration

// Wrapper for ide_read_sectors to match read_vfs_t signature for raw disk access
static uint32_t ide_read_wrapper(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    // node->ptr holds the drive ID for the main disk node
    uint8_t drive = (uint8_t)(uintptr_t)node->ptr;
    // For raw disk read, offset is LBA (assuming 512 byte sectors)
    uint32_t lba = (uint32_t)(offset / 512);
    // Number of sectors to read
    uint8_t num_sectors = (uint8_t)((size + 511) / 512); // Round up to nearest sector
    
    // Ensure size is a multiple of sector size for ide_read_sectors.
    // This wrapper will handle partial reads by reading full sectors and copying.
    uint8_t *temp_buf = NULL;
    if (size % 512 != 0) {
        // Allocate a temporary buffer for full sectors
        temp_buf = (uint8_t*)kmalloc(num_sectors * 512);
        if (!temp_buf) {
            klog(LOG_ERROR, "IDE_WRAP: Failed to allocate temp buffer for read.");
            return (uint32_t)-1;
        }
    } else {
        // If size is a multiple of 512, directly use the provided buffer.
        temp_buf = buffer;
    }

    uint32_t result = ide_read_sectors(drive, lba, num_sectors, (uint16_t*)temp_buf);

    if (result == 0 && temp_buf != buffer) {
        // If a temporary buffer was used and read was successful, copy the relevant part to the user buffer.
        memcpy(buffer, temp_buf, size);
    }
    if (temp_buf != buffer) { // If kmalloc'd, free it
        kfree(temp_buf);
    }

    return result; // Return 0 on success, (uint32_t)-1 on error
}

// Wrapper for ide_write_sectors to match write_vfs_t signature for raw disk access
static uint32_t ide_write_wrapper(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    uint8_t drive = (uint8_t)(uintptr_t)node->ptr;
    uint32_t lba = (uint32_t)(offset / 512);
    uint8_t num_sectors = (uint8_t)((size + 511) / 512); // Round up to nearest sector


    if (size % 512 != 0) {
        klog(LOG_ERROR, "IDE_WRAP: Write size %u for %s is not a multiple of 512. Partial writes not supported for raw disk.", size, node->name);
        return (uint32_t)-1;
    }
    
    return ide_write_sectors(drive, lba, num_sectors, (const uint16_t*)buffer);
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
static void ide_create_partition_node(vfs_node_t *dev_dir, vfs_node_t *disk_node, uint8_t partition_num,
                                      uint32_t lba_start, uint32_t size, uint8_t drive_id) {
    char name[MAX_FILENAME_LEN];
    ksprintf(name, "sda%u", partition_num);

    if (kyrofs_create_node(dev_dir, name, VFS_FILE | VFS_CHARDEVICE) != 0) {
        klog(LOG_ERROR, "IDE: Failed to create /dev/%s node in KyroFS.", name);
        return;
    }
    vfs_node_t *part_node = vfs_finddir(dev_dir, name);
    if (!part_node) {
        klog(LOG_ERROR, "IDE: Created /dev/%s but couldn't find it!", name);
        return;
    }

    // Allocate and store partition specific data
    ide_partition_data_t *part_data = (ide_partition_data_t*)kmalloc(sizeof(ide_partition_data_t));
    if (!part_data) {
        klog(LOG_ERROR, "IDE: Failed to allocate partition data for /dev/%s.", name);
        vfs_remove(dev_dir, name);
        return;
    }
    part_data->disk_node = disk_node;
    part_data->partition_lba_start = lba_start;
    part_data->partition_size = size;
    part_data->drive_id = drive_id;

    part_node->read = ide_partition_read_sectors;
    part_node->write = ide_partition_write_sectors;
    part_node->ioctl = ide_partition_ioctl;
    part_node->ptr = part_data; // Store the partition data here
    part_node->flags |= VFS_CHARDEVICE; // Ensure char device flag

    klog(LOG_INFO, "IDE: /dev/%s partition node registered. LBA: %u, Size: %u sectors.", name, lba_start, size);
}

// Function to read MBR and create partition nodes
static void ide_init_partitions(vfs_node_t *sda_node) {
    uint16_t* buffer = (uint16_t*)kmalloc(512);
    if (!buffer) {
        klog(LOG_ERROR, "IDE: Failed to allocate buffer for MBR.");
        return;
    }

    // Read MBR using the sda_node's read function (which is ide_read_sectors)
    // lba 0, 1 sector, buffer
    if (sda_node->read(sda_node, 0, 512, (uint8_t*)buffer) != 512) {
        klog(LOG_ERROR, "IDE: Failed to read MBR from /dev/sda. (read_sectors returned %d)", sda_node->read(sda_node, 0, 512, (uint8_t*)buffer));
        kfree(buffer);
        return;
    }

    MBR* mbr = (MBR*)buffer;
    // MBR signature is in little-endian. The value 0xAA55, when read as a uint16_t from memory,
    // would be 0x55AA on a big-endian system, but 0xAA55 on little-endian.
    // The previous check in ide_test_read was `mbr->boot_signature != 0xAA55`.
    // It should be comparing with the value as it appears in memory if it's already a uint16_t.
    // Assuming x86 is little-endian, 0xAA55 is the correct check.
    if (mbr->boot_signature != 0xAA55) {
        klog(LOG_WARN, "IDE: Invalid MBR signature for /dev/sda (found 0x%x, expected 0xAA55).", mbr->boot_signature);
        kfree(buffer);
        return;
    }

    vfs_node_t *dev_dir = vfs_finddir(vfs_root, "dev");
    if (!dev_dir) {
        klog(LOG_ERROR, "IDE: /dev directory not found for partition creation!");
        kfree(buffer);
        return;
    }

    for (int i = 0; i < 4; i++) {
        MBR_PartitionEntry *part = &mbr->partitions[i];
        if (part->type == 0x00 || part->sector_count == 0) continue; // Empty or invalid partition

        ide_create_partition_node(dev_dir, sda_node, i + 1, part->lba_first_sector, part->sector_count, (uintptr_t)sda_node->ptr);
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

    // Offset in bytes for the partition. Need to convert to LBA relative to partition start.
    // Ensure that size is a multiple of sector size for ide_read_sectors.
    // If not, read full sectors and then copy relevant bytes.
    if (size % 512 != 0) {
        klog(LOG_WARN, "IDE_PART: Read size %u for %s is not a multiple of 512. Reading full sectors.", size, node->name);
        // For simplicity, we only read whole sectors. If partial read is needed,
        // it would require reading a full sector and then copying part of it.

        return (uint32_t)-1; 
    }

    uint32_t start_lba_in_partition = offset / 512;
    uint8_t num_sectors = size / 512;
    
    // Check for read beyond partition boundary
    if (start_lba_in_partition + num_sectors > part_data->partition_size) {
        klog(LOG_ERROR, "IDE_PART: Read request for %s goes beyond partition boundary. (LBA %u + sectors %u > total %u)",
             node->name, start_lba_in_partition, num_sectors, part_data->partition_size);
        return (uint32_t)-1; // Indicate error
    }

    uint32_t absolute_lba = part_data->partition_lba_start + start_lba_in_partition;
    
    // Call the underlying disk's read function (ide_read_sectors)
    return ide_read_sectors(part_data->drive_id, absolute_lba, num_sectors, (uint16_t*)buffer);
}

static uint32_t ide_partition_write_sectors(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    ide_partition_data_t *part_data = (ide_partition_data_t*)node->ptr;
    if (!part_data) {
        klog(LOG_ERROR, "IDE_PART: Invalid partition data for node %s.", node->name);
        return (uint32_t)-1;
    }

    if (size % 512 != 0) {
        klog(LOG_WARN, "IDE_PART: Write size %u for %s is not a multiple of 512. Writing full sectors.", size, node->name);
        return (uint32_t)-1;
    }

    uint32_t start_lba_in_partition = offset / 512;
    uint8_t num_sectors = size / 512;

    // Check for write beyond partition boundary
    if (start_lba_in_partition + num_sectors > part_data->partition_size) {
        klog(LOG_ERROR, "IDE_PART: Write request for %s goes beyond partition boundary. (LBA %u + sectors %u > total %u)",
             node->name, start_lba_in_partition, num_sectors, part_data->partition_size);
        return (uint32_t)-1; // Indicate error
    }

    uint32_t absolute_lba = part_data->partition_lba_start + start_lba_in_partition;
    
    return ide_write_sectors(part_data->drive_id, absolute_lba, num_sectors, (const uint16_t*)buffer);
}

static int ide_partition_ioctl(vfs_node_t *node, int request, void* argp) {
    ide_partition_data_t *part_data = (ide_partition_data_t*)node->ptr;
    if (!part_data) {
        klog(LOG_ERROR, "IDE_PART: Invalid partition data for node %s.", node->name);
        return -1;
    }

    if (request == IDE_IOCTL_GET_DISK_INFO) {
        ide_disk_info_t *info = (ide_disk_info_t *)argp;
        if (info) {
            info->total_sectors = part_data->partition_size;
            info->bytes_per_sector = 512;
            return 0;
        }
    } else if (request == IDE_IOCTL_READ_BLOCKS) {
        ide_read_blocks_t *rb = (ide_read_blocks_t *)argp;
        if (rb) {
            // LBA passed to IOCTL is relative to partition
            uint32_t absolute_lba = part_data->partition_lba_start + rb->lba;
            // Ensure read does not go beyond partition boundary
            if (rb->lba + rb->count > part_data->partition_size) {
                klog(LOG_ERROR, "IDE_PART: IOCTL READ_BLOCKS for %s goes beyond partition boundary.", node->name);
                return -1;
            }
            return ide_read_sectors(part_data->drive_id, absolute_lba, rb->count, (uint16_t*)rb->buffer);
        }
    } else if (request == IDE_IOCTL_WRITE_BLOCKS) {
        ide_write_blocks_t *wb = (ide_write_blocks_t *)argp;
        if (wb) {
            // LBA passed to IOCTL is relative to partition
            uint32_t absolute_lba = part_data->partition_lba_start + wb->lba;
            // Ensure write does not go beyond partition boundary
            if (wb->lba + wb->count > part_data->partition_size) {
                klog(LOG_ERROR, "IDE_PART: IOCTL WRITE_BLOCKS for %s goes beyond partition boundary.", node->name);
                return -1;
            }
            return ide_write_sectors(part_data->drive_id, absolute_lba, wb->count, (const uint16_t*)wb->buffer);
        }
    } else if (request == IDE_IOCTL_FORMAT) {
        ide_ioctl_format_t *fmt_req = (ide_ioctl_format_t *)argp;
        if (fmt_req) {
            // The LBA here is relative to the partition, and fs_format will add it to partition_lba_start
            return fs_format(node, part_data->partition_lba_start + fmt_req->partition_lba, fmt_req->partition_size);
        }
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
    dev->driver = &ide_driver;
    primary_master_present = false;

    // Select master drive
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xA0); 
    
    // Send IDENTIFY
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);

    // Check if drive exists
    if (inb(ATA_PRIMARY_STATUS) == 0) {
        klog(LOG_WARN, "IDE: Primary master drive does not exist.");
        return 0;
    }

    if (ide_wait_for_ready() != 0) {
        klog(LOG_WARN, "IDE: Timed out waiting for IDENTIFY.");
        return 0;
    }

    // Check for DRQ
    if (inb(ATA_PRIMARY_STATUS) & ATA_SR_DRQ) {
        klog(LOG_INFO, "IDE: Primary master drive found and ready.");
        primary_master_present = true;
            // Read and discard IDENTIFY data
            uint16_t identify_data[256];
            insw(ATA_PRIMARY_DATA, identify_data, 256);
        
            // Create a device node in KyroFS for this IDE drive at /dev/sda
            vfs_node_t *dev_dir = vfs_finddir(vfs_root, "dev");
            if (!dev_dir) {
                klog(LOG_ERROR, "IDE: /dev directory not found in VFS! Cannot create /dev/sda.");
                return -1;
            }

            // Use kyrofs_create_node to properly create the node in the /dev tree.
            // Mark as file AND char device because it's a block device that can be read/written like a file.
            if (kyrofs_create_node(dev_dir, "sda", VFS_FILE | VFS_CHARDEVICE) != 0) {
                klog(LOG_ERROR, "IDE: Failed to create /dev/sda node in KyroFS.");
                return -1;
            }
            vfs_node_t *sda_node = vfs_finddir(dev_dir, "sda");
            if (!sda_node) {
                klog(LOG_ERROR, "IDE: Created /dev/sda but couldn't find it!");
                return -1;
            }

            // Now assign the specific IDE functions to this newly created VFS node
            sda_node->read = ide_read_wrapper;
            sda_node->write = ide_write_wrapper;
            sda_node->ioctl = ide_ioctl;
            sda_node->ptr = (void*)(uintptr_t)0; // Store drive number (0 for primary master)
            // Flags are already set by kyrofs_create_node, ensure VFS_CHARDEVICE is present.
            sda_node->flags |= VFS_CHARDEVICE;

            klog(LOG_INFO, "IDE: /dev/sda device node properly registered in VFS.");

            // Now initialize partitions for this disk
            ide_init_partitions(sda_node);
            } else {
        klog(LOG_WARN, "IDE: Primary master drive not responding correctly to IDENTIFY.");
    }

    return 0;
}

// New IOCTL handler for IDE devices
static int ide_ioctl(vfs_node_t *node, int request, void* argp) {

    // The node->ptr could be used to store a drive ID if multiple IDE drives
    uint8_t drive = 0; 

    if (request == IDE_IOCTL_GET_DISK_INFO) {
        ide_disk_info_t *info = (ide_disk_info_t *)argp;
        if (info) {

            info->total_sectors = 0x0FFFFFFF; // Example: large enough
            info->bytes_per_sector = 512;
            return 0;
        }
    } else if (request == IDE_IOCTL_READ_BLOCKS) {
        ide_read_blocks_t *rb = (ide_read_blocks_t *)argp;
        if (rb) {
            return ide_read_sectors(drive, rb->lba, rb->count, (uint16_t*)rb->buffer);
        }
    } else if (request == IDE_IOCTL_WRITE_BLOCKS) {
        ide_write_blocks_t *wb = (ide_write_blocks_t *)argp;
        if (wb) {
            return ide_write_sectors(drive, wb->lba, wb->count, (const uint16_t*)wb->buffer);
        }
    } else if (request == IDE_IOCTL_FORMAT) {
        ide_ioctl_format_t *fmt_req = (ide_ioctl_format_t *)argp;
        if (fmt_req) {
            // fs_format expects a disk_fd.
            // Since ide_ioctl is called from a vfs_node associated with a physical disk,
            // we can pass the node->ptr (which holds the drive number) as a pseudo-fd for fs_format,
            // or modify fs_format to take a vfs_node_t directly.

            return fs_format(node, fmt_req->partition_lba, fmt_req->partition_size);
        }
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
    if (drive != 0 || !primary_master_present) return (uint32_t)-1;

    if (ide_wait_for_ready() != 0) return (uint32_t)-1;

    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, num_sectors);
    outb(ATA_PRIMARY_LBA_LO, lba & 0xFF);
    outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_PIO);

    for (int i = 0; i < num_sectors; i++) {
        if (ide_wait_for_ready() != 0) return (uint32_t)-1;
        if (inb(ATA_PRIMARY_STATUS) & (ATA_SR_DF | ATA_SR_ERR)) {
            klog(LOG_ERROR, "IDE: Read error.");
            return (uint32_t)-1;
        }
        insw(ATA_PRIMARY_DATA, buffer + (i * 256), 256);
    }
    return 0;
}

uint32_t ide_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, const uint16_t* buffer) {
    if (drive != 0 || !primary_master_present) return (uint32_t)-1;
    
    if (ide_wait_for_ready() != 0) return (uint32_t)-1;

    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, num_sectors);
    outb(ATA_PRIMARY_LBA_LO, lba & 0xFF);
    outb(ATA_PRIMARY_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_PIO);

    for (int i = 0; i < num_sectors; i++) {
        if (ide_wait_for_ready() != 0) return (uint32_t)-1;
        if (inb(ATA_PRIMARY_STATUS) & (ATA_SR_DF | ATA_SR_ERR)) {
            klog(LOG_ERROR, "IDE: Write error.");
            return (uint32_t)-1;
        }
        outsw(ATA_PRIMARY_DATA, buffer + (i * 256), 256);
    }
    
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    if (ide_wait_for_ready() != 0) return (uint32_t)-1;
    
    return 0;
}

void ide_driver_init() {
    deviceman_register_driver(&ide_driver);
    klog(LOG_INFO, "IDE driver registered with Device Manager.");
}