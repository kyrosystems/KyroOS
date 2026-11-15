#include "pci.h"
#include "port_io.h"
#include "fb.h"
#include "vmm.h"
#include "display.h"
#include "kstring.h"
#include "errno.h"

/* Minimal CPU framebuffer modesetting only: no FIFO, DMA or 3D claims. */
static uint16_t svga_port;
static int adapter; /* 1 VMware SVGA II, 2 Bochs VBE */
static uint32_t reg_read(uint32_t index) {
    if (adapter == 1) { outl(svga_port, index); return inl(svga_port + 1); }
    outw(0x1ce, index); return inw(0x1cf);
}
static void reg_write(uint32_t index, uint32_t value) {
    if (adapter == 1) { outl(svga_port, index); outl(svga_port + 1, value); }
    else { outw(0x1ce, index); outw(0x1cf, value); }
}
static int detect(void) {
    if (adapter) return 0;
    const fb_info_t *fb = fb_get_info();
    uint64_t phys = (uint64_t)vmm_virt_to_phys((void *)fb->address);
    for (pci_device_node_t *n = pci_devices; n; n = n->next) {
        pci_device_t *d = &n->device;
        if (d->vendor_id == 0x15ad && d->device_id == 0x0405 && d->bars[0].type == 1) {
            svga_port = d->bars[0].base;
            adapter = 1;
            if (reg_read(0) != 0x90000002) { reg_write(0, 0x90000002); }
            if (reg_read(0) == 0x90000002 && reg_read(13) + reg_read(14) == phys) return 0;
            adapter = 0;
        }
        if ((d->vendor_id == 0x1234 || d->vendor_id == 0x80ee) && d->device_id == 0x1111 && d->bars[0].base == phys) {
            adapter = 2;
            unsigned id = reg_read(0);
            if (id >= 0xb0c0 && id <= 0xb0c5) return 0;
            adapter = 0;
        }
    }
    return -ENODEV;
}
int video_mode_info(display_mode_t *info) {
    const fb_info_t *fb = fb_get_info();
    memset(info, 0, sizeof(*info));
    info->width = fb->width; info->height = fb->height;
    info->capacity = fb_mode_capacity();
    int rc = detect();
    strcpy(info->driver, rc ? "Limine framebuffer (fixed)" : adapter == 1 ? "VMware SVGA II (CPU)" : "Bochs VBE (CPU)");
    info->supported = rc == 0;
    return 0;
}
int video_set_mode(unsigned width, unsigned height) {
    if (detect()) return -ENODEV;
    if (width < 640 || height < 480 || width > 1920 || height > 1200 || (width & 7) ||
        (uint64_t)width * height * 4 > fb_mode_capacity()) return -EINVAL;
    const fb_info_t previous = *fb_get_info();
    uint32_t pitch;
    if (adapter == 1) {
        if (width > reg_read(4) || height > reg_read(5)) return -EINVAL;
        reg_write(1, 0); reg_write(2, width); reg_write(3, height); reg_write(7, 32); reg_write(1, 1);
        pitch = reg_read(12);
        if (reg_read(2) != width || reg_read(3) != height || reg_read(7) != 32 || pitch < width * 4 ||
            (uint64_t)pitch * height > fb_mode_capacity()) {
            reg_write(1, 0); reg_write(2, previous.width); reg_write(3, previous.height); reg_write(7, 32); reg_write(1, 1);
            return -EINVAL;
        }
    } else {
        reg_write(4, 0); reg_write(1, width); reg_write(2, height); reg_write(3, 32);
        reg_write(6, width); reg_write(8, 0); reg_write(9, 0); reg_write(4, 0x41);
        pitch = width * 4;
    }
    fb_update_mode(width, height, pitch);
    return 0;
}
