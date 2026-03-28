#ifndef INSTALLER_H
#define INSTALLER_H

#include "ide.h" 
#include <stdint.h>
#include <stddef.h> 

// MBR Partition Entry Structure
typedef struct {
    uint8_t boot_flag;      
    uint8_t starting_chs[3]; 
    uint8_t type_code;      
    uint8_t ending_chs[3];   
    uint32_t starting_lba;   
    uint32_t size_in_sectors; 
} __attribute__((packed)) mbr_partition_entry_t;

// Master Boot Record Structure
typedef struct {
    uint8_t boot_code[440]; 
    uint32_t signature;     
    uint16_t reserved;      
    mbr_partition_entry_t partitions[4]; 
    uint16_t boot_signature; 
} __attribute__((packed)) mbr_t;

// Kernel function
void installer_start();

// Userspace tool functions (for compatibility)
void installer_disk_detection();
void installer_partition_selection();
void installer_filesystem_formatting();
void installer_base_system_installation();
void installer_install_initial_packages();
void installer_install_bootloader();
void installer_create_initial_user();
void installer_basic_configuration();

#endif // INSTALLER_H
