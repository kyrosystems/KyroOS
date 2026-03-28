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
    outl(0xCFC, (inl(0xCFC) & ~(0xFFFF << ((offset & 2) * 8))) | (data << ((offset & 2) * 8)));
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
    memset(node, 0, sizeof(pci_device_node_t));
    pci_device_t *dev = &node->device;
    
    dev->bus = bus; dev->device = slot; dev->func = func;
    dev->vendor_id = vendor;
    dev->device_id = pci_config_read_word(bus, slot, func, 2);
    
    uint32_t class_reg = pci_config_read_dword(bus, slot, func, 0x08);
    dev->class_code = (class_reg >> 24) & 0xFF;
    dev->subclass = (class_reg >> 16) & 0xFF;
    dev->prog_if = (class_reg >> 8) & 0xFF;

    for (int i = 0; i < 6; i++) {
        uint32_t bar = pci_config_read_dword(bus, slot, func, 0x10 + (i * 4));
        if (bar & 1) { dev->bars[i].base = bar & ~0x3; dev->bars[i].type = 1; }
        else { dev->bars[i].base = bar & ~0xF; dev->bars[i].type = 0; }
    }

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

    // Fallback for Intel E1000 (0x8086:0x100E or 0x100F)
    if (dev->vendor_id == 0x8086 && (dev->device_id == 0x100E || dev->device_id == 0x100F)) {
        extern void e1000_driver_init(pci_device_t *dev);
        klog(LOG_INFO, "PCI: Forced bind E1000 to %02x:%02x.%x", bus, slot, func);
        e1000_driver_init(dev);
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
