#include "ac97.h"
#include "audio.h"
#include "deviceman.h" // For deviceman_register_driver
#include "driver.h"
#include "heap.h"
#include "isr.h"
#include "kstring.h" // For memset, memcpy
#include "log.h"
#include "pci.h"
#include "pmm.h" // For pmm_alloc_page
#include "port_io.h"

// AC'97 Register Offsets (Native Audio Mixer Bus Master)
#define AC97_NAM_RESET 0x00
#define AC97_NAM_MASTER_VOLUME 0x02
#define AC97_NAM_PCM_OUT_VOLUME 0x18
#define AC97_BDBAR                                                             \
  0x10                 // Bus Master Audio - Buffer Descriptor List Base Address
#define AC97_CIV 0x14  // Current Index Value
#define AC97_LVI 0x15  // Last Valid Index
#define AC97_SR 0x16   // Status Register
#define AC97_PICB 0x18 // PIO Command Bar
#define PIV 0x1A       // PIO Index Value
#define AC97_CR 0x1B   // Control Register

// Buffer Descriptor List entry
typedef struct {
  uint32_t buffer_pointer;
  uint16_t length;
  uint16_t flags; // b15: IOC (Interrupt on Completion)
} __attribute__((packed)) bdl_entry_t;

#define AC97_BDL_ENTRIES 32

typedef struct {
  device_t *pci_dev;
  uint32_t nabm_bar; // Native Audio Bus Mastering BAR
  bdl_entry_t *bdl;
  uint32_t bdl_phys;
  int16_t *buffer;
  uint32_t buffer_phys;
  uint8_t irq;
  volatile uint8_t current_bdl_index;
} ac97_dev_t;

static ac97_dev_t *main_ac97_device = NULL;

static void ac97_play_buffer(const int16_t *buffer, size_t size);

static int ac97_probe(device_t *dev) {
  // Class 0x04 (Multimedia), Subclass 0x01 (Audio)
  if (pci_get_class(dev->bus, dev->device, dev->func) == 0x04 &&
      pci_get_subclass(dev->bus, dev->device, dev->func) == 0x01) {
    klog(LOG_INFO, "AC97: Found audio device.");
    return 1;
  }
  return 0;
}

static void ac97_interrupt_handler(struct registers *regs) {
  (void)regs; // Suppress unused parameter warning
  if (main_ac97_device) {
    uint16_t status = inw(main_ac97_device->nabm_bar + AC97_SR);
    if (status & 0x04) { // Interrupt on Completion
      klog(LOG_INFO, "AC97: Playback of a buffer completed.");
      // Handle next buffer in a queue, etc.
    }
    // Acknowledge interrupt by clearing status bits
    outw(main_ac97_device->nabm_bar + AC97_SR, status);
  }
}

static int ac97_attach(device_t *dev) {
  klog(LOG_INFO, "AC97: Attaching driver...");

  ac97_dev_t *ac97_dev = (ac97_dev_t *)kmalloc(sizeof(ac97_dev_t));
  if (!ac97_dev) {
    klog(LOG_ERROR, "AC97: Failed to allocate device structure.");
    return -1;
  }
  memset(ac97_dev, 0, sizeof(ac97_dev_t));
  ac97_dev->pci_dev = dev;
  dev->private_data = ac97_dev;
  ac97_dev->buffer = NULL; // Initialize buffer to NULL

  // Enable Bus Mastering
  uint16_t pci_cmd =
      pci_read_config_word(dev->bus, dev->device, dev->func, PCI_COMMAND);
  pci_write_config_word(dev->bus, dev->device, dev->func, PCI_COMMAND,
                        pci_cmd | (1 << 2));

  // Get BAR for Native Audio Bus Mastering
  ac97_dev->nabm_bar =
      pci_read_config_dword(dev->bus, dev->device, dev->func, 0x14) & 0xFFFE;

  // Allocate BDL
  ac97_dev->bdl = (bdl_entry_t *)pmm_alloc_page();
  ac97_dev->bdl_phys = (uint32_t)(uint64_t)ac97_dev->bdl;
  memset(ac97_dev->bdl, 0, sizeof(bdl_entry_t) * AC97_BDL_ENTRIES);

  // Set BDL base address
  outl(ac97_dev->nabm_bar + AC97_BDBAR, ac97_dev->bdl_phys);

  // Reset and enable interrupts
  outw(ac97_dev->nabm_bar + AC97_NAM_RESET, 1); // Reset
  outw(ac97_dev->nabm_bar + AC97_NAM_MASTER_VOLUME,
       0x0000); // Unmute, max volume

  // Register IRQ handler
  ac97_dev->irq = pci_read_config_byte(dev->bus, dev->device, dev->func, 0x3C);
  register_irq_handler(ac97_dev->irq, ac97_interrupt_handler);

  main_ac97_device = ac97_dev;
  audio_register_driver(ac97_play_buffer);

  klog(LOG_INFO, "AC97 driver attached successfully.");
  return 0;
}

static void ac97_play_buffer(const int16_t *buffer, size_t size) {
  if (!main_ac97_device)
    return;

  // Stop playback first
  outb(main_ac97_device->nabm_bar + AC97_CR, 0);

  // For this simple implementation, we use one BDL entry and one buffer
  // A real driver would use a ring buffer and queueing.
  if (main_ac97_device->buffer)
    kfree(main_ac97_device->buffer); // Free old buffer

  main_ac97_device->buffer = (int16_t *)kmalloc(size);
  memcpy(main_ac97_device->buffer, buffer, size);
  main_ac97_device->buffer_phys = (uint32_t)(uint64_t)main_ac97_device->buffer;

  main_ac97_device->bdl[0].buffer_pointer = main_ac97_device->buffer_phys;
  main_ac97_device->bdl[0].length = size / 2; // Length in samples (16-bit)
  main_ac97_device->bdl[0].flags = (1 << 15); // Interrupt on completion

  // Set Last Valid Index
  outb(main_ac97_device->nabm_bar + AC97_LVI, 0);

  // Start playback on PCM out channel
  outb(main_ac97_device->nabm_bar + AC97_CR, 1);

  klog(LOG_INFO, "AC97: Started playback.");
}

static driver_t ac97_driver = {
    .name = "ac97_audio",
    .probe = ac97_probe,
    .attach = ac97_attach,
};

void ac97_driver_init() {
  deviceman_register_driver(&ac97_driver);
  klog(LOG_INFO, "AC97 driver registered.");
}
