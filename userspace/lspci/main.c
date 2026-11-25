#include <kyroolib.h>
#include <stdint.h>

#define MAX_PCI_DEVS 64

static const char *get_vendor_name(uint16_t vendor) {
    switch (vendor) {
        case 0x8086: return "Intel Corporation";
        case 0x1002: return "Advanced Micro Devices, Inc. [AMD/ATI]";
        case 0x10DE: return "NVIDIA Corporation";
        case 0x15AD: return "VMware";
        case 0x1AF4: return "Red Hat, Inc. (Virtio)";
        case 0x10EC: return "Realtek Semiconductor Co., Ltd.";
        case 0x1234: return "Bochs/QEMU";
        case 0x80EE: return "InnoTek Systemberatung GmbH (VirtualBox)";
        default:     return "Unknown Vendor";
    }
}

static const char *get_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case 0x01:
            switch (subclass) {
                case 0x01: return "IDE interface";
                case 0x06: return "SATA controller (AHCI)";
                case 0x08: return "Non-Volatile memory controller (NVMe)";
                default:   return "Mass storage controller";
            }
        case 0x02:
            switch (subclass) {
                case 0x00: return "Ethernet controller";
                case 0x80: return "Network controller";
                default:   return "Network controller";
            }
        case 0x03:
            switch (subclass) {
                case 0x00: return "VGA compatible controller";
                default:   return "Display controller";
            }
        case 0x04:
            switch (subclass) {
                case 0x01: return "Multimedia audio controller";
                case 0x03: return "Audio device";
                default:   return "Multimedia controller";
            }
        case 0x06:
            switch (subclass) {
                case 0x00: return "Host bridge";
                case 0x01: return "ISA bridge";
                case 0x04: return "PCI bridge";
                default:   return "Bridge";
            }
        case 0x0C:
            switch (subclass) {
                case 0x03: return "USB controller";
                case 0x05: return "SMBus";
                default:   return "Serial bus controller";
            }
        default:
            return "Unknown Device";
    }
}

int main(int argc, char **argv) {
    bool numeric = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print("Usage: lspci [-n]\n");
            print("List all PCI devices connected to the system.\n");
            print_flush();
            return 0;
        }
        if (strcmp(argv[i], "-n") == 0) numeric = true;
    }

    struct pci_dev_info devs[MAX_PCI_DEVS];
    int n = pci_list(devs, MAX_PCI_DEVS);
    if (n < 0) {
        print("lspci: cannot access PCI bus\n");
        print_flush();
        return 1;
    }

    for (int i = 0; i < n; i++) {
        if (numeric) {
            printf("%02x:%02x.%x %04x: %04x:%04x (rev %02x)\n",
                   devs[i].bus, devs[i].slot, devs[i].func,
                   ((uint16_t)devs[i].class_code << 8) | devs[i].subclass,
                   devs[i].vendor_id, devs[i].device_id,
                   devs[i].prog_if);
        } else {
            const char *cls = get_class_name(devs[i].class_code, devs[i].subclass);
            const char *vnd = get_vendor_name(devs[i].vendor_id);
            printf("%02x:%02x.%x %s: %s [Device %04x:%04x]\n",
                   devs[i].bus, devs[i].slot, devs[i].func,
                   cls, vnd,
                   devs[i].vendor_id, devs[i].device_id);
        }
    }
    print_flush();
    return 0;
}
