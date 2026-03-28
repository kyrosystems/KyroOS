#ifndef LKM_H
#define LKM_H

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"

// Module initialization function type
typedef int (*lkm_init_t)(void);
// Module exit function type
typedef void (*lkm_exit_t)(void);

// Module structure
typedef struct kernel_module {
    char name[64];              // Module name
    void *base;                 // Base address in memory
    size_t size;                // Total size
    lkm_init_t init;            // Init function
    lkm_exit_t exit;            // Exit function
    int refcount;               // Reference count
    struct kernel_module *next; // Linked list
} kernel_module_t;

// LKM Manager structure
typedef struct {
    kernel_module_t *modules;   // List of loaded modules
    int module_count;           // Number of loaded modules
} lkm_manager_t;

// Module header (at beginning of .kmod file)
#define KMOD_MAGIC 0x4B4D4F445F4B524F  // "KMOD_KRO"
#define KMOD_VERSION 1

typedef struct {
    uint64_t magic;
    uint32_t version;
    char name[64];
    uint64_t init_offset;
    uint64_t exit_offset;
    uint64_t text_size;
    uint64_t data_size;
} kmod_header_t;

// Functions
void lkm_manager_init(lkm_manager_t *mgr);
int lkm_load_module(lkm_manager_t *mgr, const char *path);
int lkm_unload_module(lkm_manager_t *mgr, const char *name);
kernel_module_t *lkm_find_module(lkm_manager_t *mgr, const char *name);
void lkm_list_modules(lkm_manager_t *mgr);
int lkm_load_from_buffer(const uint8_t *data, size_t size, kernel_module_t **out_module);
void lkm_unload_module_internal(kernel_module_t *mod);

// Global LKM manager
extern lkm_manager_t g_lkm_manager;

#endif // LKM_H
