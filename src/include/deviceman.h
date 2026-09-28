#ifndef DEVICEMAN_H
#define DEVICEMAN_H

#include "driver.h"

void deviceman_init();
void deviceman_register_driver(driver_t* driver);
void deviceman_register_device(device_t* device);
void deviceman_probe_devices(); // Iterates through all registered drivers and devices to find matches

#endif // DEVICEMAN_H
