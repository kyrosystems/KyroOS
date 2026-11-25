#include "virtio.h"
#include "port_io.h"
#include "log.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "kstring.h"
#include "devfs.h"
#include "vfs.h"
#include "ide.h" // For ide_read_blocks_t / ide_write_blocks_t ioctl interface

static uint16_t g_io_base = 0;
static uint16_t g_qsize = 0;
static vring_desc_t *g_desc = NULL;
static vring_avail_t *g_avail = NULL;
static vring_used_t *g_used = NULL;
static uint64_t g_capacity_sectors = 0;

static int virtio_blk_rw(uint64_t sector, uint32_t count, void *buffer, int is_write) {
    if (!g_io_base || !g_desc || !g_avail || g_qsize == 0) return -1;

    // Header, data, status
    static virtio_blk_req_t req __attribute__((aligned(16)));
    static volatile uint8_t status __attribute__((aligned(16)));

    req.type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req.reserved = 0;
    req.sector = sector;
    status = 0xFF;

    // Desc 0: Header
    g_desc[0].addr = (uint64_t)vmm_virt_to_phys(&req);
    g_desc[0].len = sizeof(virtio_blk_req_t);
    g_desc[0].flags = VRING_DESC_F_NEXT;
    g_desc[0].next = 1;

    // Desc 1: Buffer
    g_desc[1].addr = (uint64_t)vmm_virt_to_phys(buffer);
    g_desc[1].len = count * 512;
    g_desc[1].flags = VRING_DESC_F_NEXT | (is_write ? 0 : VRING_DESC_F_WRITE);
    g_desc[1].next = 2;

    // Desc 2: Status
    g_desc[2].addr = (uint64_t)vmm_virt_to_phys((void*)&status);
    g_desc[2].len = 1;
    g_desc[2].flags = VRING_DESC_F_WRITE;
    g_desc[2].next = 0;

    // Put into available ring
    uint16_t avail_idx = g_avail->idx;
    g_avail->ring[avail_idx % g_qsize] = 0;
    __asm__ volatile("" ::: "memory");
    g_avail->idx = avail_idx + 1;

    // Notify queue 0
    outw(g_io_base + VIRTIO_REG_QUEUE_NOTIFY, 0);

    // Wait for completion
    uint32_t timeout = 0;
    while (status == 0xFF) {
        if (++timeout > 1000000) {
            klog(LOG_ERROR, "virtio-blk: Command timed out at sector %llu", sector);
            return -1;
        }
        __asm__ volatile("pause");
    }

    return (status == 0) ? 0 : -1;
}

static int virtio_blk_ioctl(vfs_node_t *node, int request, void *arg) {
    (void)node;
    if (!arg) return -1;
    if (request == IDE_IOCTL_READ_BLOCKS) {
        ide_read_blocks_t *rb = (ide_read_blocks_t *)arg;
        if (!rb->buffer) return -1;
        if (rb->count == 0) return 0;
        if ((uint64_t)rb->lba + rb->count > g_capacity_sectors) return -1;
        if ((uint64_t)rb->count * 512 > 0xFFFFFFFFULL) return -1;
        return virtio_blk_rw(rb->lba, rb->count, rb->buffer, 0);
    } else if (request == IDE_IOCTL_WRITE_BLOCKS) {
        ide_write_blocks_t *wb = (ide_write_blocks_t *)arg;
        if (!wb->buffer) return -1;
        if (wb->count == 0) return 0;
        if ((uint64_t)wb->lba + wb->count > g_capacity_sectors) return -1;
        if ((uint64_t)wb->count * 512 > 0xFFFFFFFFULL) return -1;
        return virtio_blk_rw(wb->lba, wb->count, (void *)(uintptr_t)wb->buffer, 1);
    }
    return -1;
}

void virtio_blk_init(pci_device_t *dev) {
    klog(LOG_INFO, "virtio-blk: Initializing VirtIO Block Device...");

    g_io_base = (uint16_t)dev->bars[0].base;
    if (!g_io_base) {
        klog(LOG_ERROR, "virtio-blk: Invalid I/O BAR0.");
        return;
    }

    // Reset and acknowledge
    outb(g_io_base + VIRTIO_REG_DEVICE_STATUS, 0);
    outb(g_io_base + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    // Read capacity at offset 0x14
    uint32_t cap_lo = inl(g_io_base + 0x14);
    uint32_t cap_hi = inl(g_io_base + 0x18);
    g_capacity_sectors = ((uint64_t)cap_hi << 32) | cap_lo;
    klog(LOG_INFO, "virtio-blk: Capacity: %llu MB (%llu sectors)",
         (g_capacity_sectors * 512) / (1024 * 1024), g_capacity_sectors);

    // Setup queue 0
    outw(g_io_base + VIRTIO_REG_QUEUE_SELECT, 0);
    g_qsize = inw(g_io_base + VIRTIO_REG_QUEUE_SIZE);
    if (!g_qsize) {
        klog(LOG_ERROR, "virtio-blk: Queue 0 unavailable.");
        return;
    }

    // Allocate queue memory (page aligned)
    void *q_phys = pmm_alloc_pages(2);
    if (!q_phys) {
        klog(LOG_ERROR, "virtio-blk: Failed to allocate queue memory.");
        return;
    }
    void *q_virt = vmm_phys_to_virt(q_phys);
    memset(q_virt, 0, PAGE_SIZE * 2);

    g_desc = (vring_desc_t *)q_virt;
    g_avail = (vring_avail_t *)((uint8_t *)q_virt + g_qsize * sizeof(vring_desc_t));
    uint64_t used_offset = ((uint64_t)g_avail + sizeof(vring_avail_t) + g_qsize * sizeof(uint16_t) + 4095) & ~4095ULL;
    g_used = (vring_used_t *)used_offset;

    // Tell device the physical page number
    outl(g_io_base + VIRTIO_REG_QUEUE_ADDRESS, (uint32_t)((uint64_t)q_phys >> 12));

    // Driver ready
    outb(g_io_base + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    vfs_node_t *vda = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (vda) {
        memset(vda, 0, sizeof(vfs_node_t));
        strncpy(vda->name, "vda", sizeof(vda->name) - 1);
        vda->flags = VFS_BLOCKDEVICE | VFS_CHARDEVICE;
        vda->inode = vfs_get_next_inode();
        vda->ioctl = virtio_blk_ioctl;
        vda->length = g_capacity_sectors * 512;
        vda->mode = 0660;
        devfs_register_device("vda", vda);
        klog(LOG_INFO, "virtio-blk: Registered /dev/vda");
    }
}
