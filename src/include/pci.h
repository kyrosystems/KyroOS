#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stddef.h>

#define PCI_COMMAND 0x04
#define PCI_STATUS 0x06
#define PCI_CLASS_REVISION 0x08
#define PCI_BIST_HEADER_TYPE 0x0C
#define PCI_BAR0 0x10
#define PCI_BAR1 0x14
#define PCI_BAR2 0x18
#define PCI_BAR3 0x1C
#define PCI_BAR4 0x20
#define PCI_BAR5 0x24
#define PCI_CAPABILITY_LIST 0x34
#define PCI_INTERRUPT_LINE 0x3C

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t func;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    struct {
        uint64_t base;
        uint64_t size;
        uint8_t type;
    } bars[6];
} pci_device_t;

typedef struct pci_device_node {
    pci_device_t device;
    struct pci_device_node *next;
} pci_device_node_t;

extern pci_device_node_t *pci_devices;

typedef struct pci_driver {
    const char *name;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    void (*init)(pci_device_t *dev);
    struct pci_driver *next;
} pci_driver_t;

void pci_init();
void pci_register_driver(pci_driver_t *driver);
void pci_enumerate();

uint16_t pci_config_read_word(uint8_t b, uint8_t s, uint8_t f, uint8_t o);
void pci_config_write_word(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint16_t d);
uint32_t pci_config_read_dword(uint8_t b, uint8_t s, uint8_t f, uint8_t o);
void pci_config_write_dword(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint32_t d);
uint8_t pci_config_read_byte(uint8_t b, uint8_t s, uint8_t f, uint8_t o);

#define pci_read_config_word pci_config_read_word
#define pci_write_config_word pci_config_write_word
#define pci_read_config_dword pci_config_read_dword
#define pci_write_config_dword pci_config_write_dword
#define pci_read_config_byte pci_config_read_byte

static inline uint8_t pci_get_class(uint8_t b, uint8_t s, uint8_t f) { return (pci_config_read_dword(b, s, f, 0x08) >> 24) & 0xFF; }
static inline uint8_t pci_get_subclass(uint8_t b, uint8_t s, uint8_t f) { return (pci_config_read_dword(b, s, f, 0x08) >> 16) & 0xFF; }

#endif
