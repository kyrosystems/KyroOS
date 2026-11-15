#include "pci.h"
#include "port_io.h"
#include "log.h"
#include "heap.h"
#include "kstring.h"

static pci_driver_t *drivers_list = NULL;
pci_device_node_t *pci_devices = NULL;

uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return (uint16_t)((inl(0xCFC) >> ((offset & 2) * 8)) & 0xFFFF);
}

uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return inl(0xCFC);
}

uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return (uint8_t)((inl(0xCFC) >> ((offset & 3) * 8)) & 0xFF);
}

void pci_config_write_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t data) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    outw(0xCFC + (offset & 2), data);
}

void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    outl(0xCFC, data);
}

void pci_register_driver(pci_driver_t *driver) {
    driver->next = drivers_list;
    drivers_list = driver;
}

static void pci_probe_device(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor = pci_config_read_word(bus, slot, func, 0);
    if (vendor == 0xFFFF) return;

    pci_device_node_t *node = kmalloc(sizeof(pci_device_node_t));
    if (!node) return;
    memset(node, 0, sizeof(pci_device_node_t));
    pci_device_t *dev = &node->device;
    
    dev->bus = bus; dev->device = slot; dev->func = func;
    dev->vendor_id = vendor;
    dev->device_id = pci_config_read_word(bus, slot, func, 2);
    
    uint32_t class_reg = pci_config_read_dword(bus, slot, func, 0x08);
    dev->class_code = (class_reg >> 24) & 0xFF;
    dev->subclass = (class_reg >> 16) & 0xFF;
    dev->prog_if = (class_reg >> 8) & 0xFF;

    uint8_t header = pci_config_read_byte(bus, slot, func, 0x0E) & 0x7F;
    int bar_count = header == 0 ? 6 : (header == 1 ? 2 : 0);
    uint16_t command = pci_config_read_word(bus, slot, func, PCI_COMMAND);
    // BAR sizing must not temporarily decode the all-ones probe addresses
    pci_config_write_word(bus, slot, func, PCI_COMMAND, command & ~3);
    for (int i = 0; i < bar_count; i++) {
        uint8_t off = PCI_BAR0 + i * 4;
        uint32_t low = pci_config_read_dword(bus, slot, func, off);
        if (low == 0xFFFFFFFF) continue;
        bool io = (low & 1) != 0;
        bool wide = !io && ((low >> 1) & 3) == 2 && i + 1 < bar_count;
        uint32_t high = wide ? pci_config_read_dword(bus, slot, func, off + 4) : 0;
        pci_config_write_dword(bus, slot, func, off, 0xFFFFFFFF);
        if (wide) pci_config_write_dword(bus, slot, func, off + 4, 0xFFFFFFFF);
        uint32_t mask_low = pci_config_read_dword(bus, slot, func, off);
        uint32_t mask_high = wide ? pci_config_read_dword(bus, slot, func, off + 4) : 0;
        pci_config_write_dword(bus, slot, func, off, low);
        if (wide) pci_config_write_dword(bus, slot, func, off + 4, high);
        uint64_t mask = (uint64_t)(mask_low & (io ? ~3U : ~15U));
        dev->bars[i].base = (uint64_t)(low & (io ? ~3U : ~15U));
        dev->bars[i].type = io ? 1 : 0;
        if (wide) {
            mask |= (uint64_t)mask_high << 32;
            dev->bars[i].base |= (uint64_t)high << 32;
            dev->bars[i].size = mask ? ~mask + 1 : 0;
            i++; // Upper half belongs to this BAR, not another device aperture
        } else {
            dev->bars[i].size = mask ? (uint32_t)(~(uint32_t)mask + 1) : 0;
        }
    }
    pci_config_write_word(bus, slot, func, PCI_COMMAND, command);

    // Add to global list
    node->next = pci_devices;
    pci_devices = node;

    pci_driver_t *drv = drivers_list;
    while (drv) {
        bool match = false;
        if (drv->vendor_id != 0 && drv->vendor_id == dev->vendor_id && drv->device_id == dev->device_id) match = true;
        else if (drv->vendor_id == 0 && drv->class_code == dev->class_code && drv->subclass == dev->subclass) match = true;

        if (match) {
            klog(LOG_INFO, "PCI: Binding driver '%s' to device %02x:%02x.%x", drv->name, bus, slot, func);
            drv->init(dev);
            break;
        }
        drv = drv->next;
    }

    // Intel Ethernet Controllers (E1000 / E1000e / I217 / I218 / I219)
    if (dev->vendor_id == 0x8086 && (dev->device_id == 0x100E || dev->device_id == 0x100F ||
                                     dev->device_id == 0x10D3 || dev->device_id == 0x1079 ||
                                     dev->device_id == 0x107C || dev->device_id == 0x10EA ||
                                     dev->device_id == 0x153A || dev->device_id == 0x153B ||
                                     dev->device_id == 0x1559 || dev->device_id == 0x155A ||
                                     dev->device_id == 0x15A1 || dev->device_id == 0x15A2 ||
                                     dev->device_id == 0x1570 || dev->device_id == 0x15B8 ||
                                     dev->device_id == 0x15D8 ||
                                     (dev->class_code == 0x02 && dev->subclass == 0x00))) {
        extern void e1000_driver_init(pci_device_t *dev);
        klog(LOG_INFO, "PCI: Forced bind E1000 to %02x:%02x.%x (dev 0x%04x)", bus, slot, func, dev->device_id);
        e1000_driver_init(dev);
    }

    // AHCI SATA Controller (Class 0x01, Subclass 0x06)
    if (dev->class_code == 0x01 && dev->subclass == 0x06) {
        extern void ahci_init(pci_device_t *dev);
        klog(LOG_INFO, "PCI: Found AHCI SATA controller at %02x:%02x.%x", bus, slot, func);
        ahci_init(dev);
    }

    // VirtIO Devices (Vendor 0x1AF4)
    if (dev->vendor_id == 0x1AF4) {
        if (dev->device_id == 0x1000 || dev->device_id == 0x1041) {
            extern void virtio_net_init(pci_device_t *dev);
            klog(LOG_INFO, "PCI: Binding virtio-net at %02x:%02x.%x", bus, slot, func);
            virtio_net_init(dev);
        } else if (dev->device_id == 0x1001 || dev->device_id == 0x1042) {
            extern void virtio_blk_init(pci_device_t *dev);
            klog(LOG_INFO, "PCI: Binding virtio-blk at %02x:%02x.%x", bus, slot, func);
            virtio_blk_init(dev);
        }
    }
}

void pci_enumerate() {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_config_read_word(bus, slot, 0, 0);
            if (vendor == 0xFFFF) continue;
            
            pci_probe_device(bus, slot, 0);
            
            uint8_t header_type = pci_config_read_byte(bus, slot, 0, 0x0E);
            if (header_type & 0x80) { // Multi-function
                for (uint8_t func = 1; func < 8; func++) {
                    pci_probe_device(bus, slot, func);
                }
            }
        }
    }
}

void pci_init() { pci_enumerate(); }
