#ifndef E1000_H
#define E1000_H

#include "driver.h" // For driver_t and device_t
#include "net.h"    // For net_dev_t
#include <stdint.h>

extern uint64_t kernel_hhdm_offset;

// Function to initialize the E1000 driver
void e1000_driver_init();

#endif // E1000_H
