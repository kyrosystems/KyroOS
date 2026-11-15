#include "lkm.h"
#include "elf.h"
#include "log.h"
#include "heap.h"
#include "kstring.h"
#include "pmm.h"
#include "vmm.h"
#include "devfs.h"
#include "isr.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Global LKM manager instance
lkm_manager_t g_lkm_manager;

// Forward declarations for kernel functions
extern void schedule(void);
extern int thread_create(void (*entry)(void *), void *arg);
extern void vfs_close(vfs_node_t *node);

// Kernel symbol table for module relocations
typedef struct {
    const char *name;
    uint64_t address;
} kernel_symbol_t;

// List of exported kernel symbols
static kernel_symbol_t kernel_symbols[] = {
    // Logging & Printing
    {"klog", (uint64_t)klog},
    {"klog_print_str", (uint64_t)klog_print_str},
    {"klog_putchar", (uint64_t)klog_putchar},
    {"ksprintf", (uint64_t)ksprintf},
    {"snprintf", (uint64_t)ksprintf},

    // Memory
    {"kmalloc", (uint64_t)kmalloc},
    {"kfree", (uint64_t)kfree},
    {"pmm_alloc_page", (uint64_t)pmm_alloc_page},
    {"pmm_free_page", (uint64_t)pmm_free_page},
    {"vmm_map_page", (uint64_t)vmm_map_page},
    {"vmm_unmap_page", (uint64_t)vmm_unmap_page},

    // String & Memory operations
    {"memcpy", (uint64_t)memcpy},
    {"memset", (uint64_t)memset},
    {"strlen", (uint64_t)strlen},
    {"strcmp", (uint64_t)strcmp},
    {"strncmp", (uint64_t)strncmp},
    {"strcpy", (uint64_t)strcpy},
    {"strncpy", (uint64_t)strncpy},

    // VFS & DevFS
    {"vfs_read", (uint64_t)vfs_read},
    {"vfs_write", (uint64_t)vfs_write},
    {"vfs_finddir", (uint64_t)vfs_finddir},
    {"vfs_resolve_path", (uint64_t)vfs_resolve_path},
    {"devfs_register_device", (uint64_t)devfs_register_device},

    // Scheduler & Timer
    {"schedule", (uint64_t)schedule},
    {"thread_create", (uint64_t)thread_create},
    {"timer_get_ticks", (uint64_t)timer_get_ticks},

    // Interrupts
    {"disable_interrupts", (uint64_t)disable_interrupts},
    {"enable_interrupts", (uint64_t)enable_interrupts},

    {NULL, 0} // Sentinel
};

static uint64_t find_kernel_symbol(const char *name) {
    for (int i = 0; kernel_symbols[i].name != NULL; i++) {
        if (strcmp(kernel_symbols[i].name, name) == 0) {
            return kernel_symbols[i].address;
        }
    }
    return 0;
}

// DevFS interface for /dev/lkm
static uint32_t lkm_dev_read(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    char text[1024];
    int len = 0;
    kernel_module_t *mod = g_lkm_manager.modules;
    len += ksprintf(text + len, "MODULE               SIZE     ADDRESS            REFS\n");
    len += ksprintf(text + len, "-----------------------------------------------------\n");
    if (!mod) {
        len += ksprintf(text + len, "(no modules loaded)\n");
    } else {
        while (mod && len < 900) {
            len += ksprintf(text + len, "%s  %u bytes  addr:0x%x  refs:%d\n",
                            mod->name, (uint32_t)mod->size, (uint64_t)mod->base, mod->refcount);
            mod = mod->next;
        }
    }
    if (offset >= (uint64_t)len) return 0;
    uint32_t avail = len - offset;
    if (size > avail) size = avail;
    memcpy(buffer, text + offset, size);
    return size;
}

static uint32_t lkm_dev_write(vfs_node_t *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    char cmd[128];
    if (size >= sizeof(cmd)) size = sizeof(cmd) - 1;
    memcpy(cmd, buffer, size);
    cmd[size] = '\0';
    // Trim newlines/spaces
    for (int i = 0; i < (int)size; i++) {
        if (cmd[i] == '\r' || cmd[i] == '\n' || cmd[i] == ' ') {
            cmd[i] = '\0';
            break;
        }
    }
    if (cmd[0] == '-') {
        // Unload command: -modulename
        lkm_unload_module(&g_lkm_manager, cmd + 1);
    } else if (cmd[0] != '\0') {
        // Load command: /path/to/module.ko
        lkm_load_module(&g_lkm_manager, cmd);
    }
    return size;
}

static int lkm_dev_ioctl(vfs_node_t *node, int request, void *argp) {
    (void)node;
    if (!argp) return -1;
    if (request == 1) { // Load path
        return lkm_load_module(&g_lkm_manager, (const char *)argp);
    } else if (request == 2) { // Unload name
        return lkm_unload_module(&g_lkm_manager, (const char *)argp);
    }
    return -1;
}

void lkm_manager_init(lkm_manager_t *mgr) {
    mgr->modules = NULL;
    mgr->module_count = 0;

    // Register /dev/lkm node for userspace inspection and control
    vfs_node_t *dev = kmalloc(sizeof(vfs_node_t));
    if (dev) {
        memset(dev, 0, sizeof(vfs_node_t));
        strncpy(dev->name, "lkm", MAX_FILENAME_LEN - 1);
        dev->flags = VFS_CHARDEVICE;
        dev->mode = 0644;
        dev->read = lkm_dev_read;
        dev->write = lkm_dev_write;
        dev->ioctl = lkm_dev_ioctl;
        devfs_register_device("lkm", dev);
    }

    klog(LOG_INFO, "LKM: Module manager initialized, /dev/lkm registered.");
}

kernel_module_t *lkm_find_module(lkm_manager_t *mgr, const char *name) {
    kernel_module_t *mod = mgr->modules;
    while (mod) {
        if (strcmp(mod->name, name) == 0) {
            return mod;
        }
        mod = mod->next;
    }
    return NULL;
}

int lkm_load_from_buffer(const uint8_t *data, size_t size, kernel_module_t **out_module) {
    klog(LOG_INFO, "LKM: Loading module from buffer (%u bytes).", size);

    if (size < sizeof(Elf64_Ehdr)) {
        klog(LOG_ERROR, "LKM: Module too small.");
        return -1;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;

    // Verify ELF header
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        klog(LOG_ERROR, "LKM: Invalid ELF magic.");
        return -1;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        klog(LOG_ERROR, "LKM: Not a 64-bit ELF.");
        return -1;
    }

    if (ehdr->e_type != ET_REL) {
        klog(LOG_ERROR, "LKM: Not a relocatable ELF (ET_REL).");
        return -1;
    }

    const Elf64_Shdr *shdr = (const Elf64_Shdr *)(data + ehdr->e_shoff);

    // Track loaded addresses for each section
    uint64_t *section_addrs = kmalloc(ehdr->e_shnum * sizeof(uint64_t));
    if (!section_addrs) {
        klog(LOG_ERROR, "LKM: Failed to allocate section table.");
        return -1;
    }
    memset(section_addrs, 0, ehdr->e_shnum * sizeof(uint64_t));

    // Calculate total size for SHF_ALLOC sections
    size_t total_size = 0;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_flags & 0x2 /* SHF_ALLOC */) {
            size_t align = shdr[i].sh_addralign ? shdr[i].sh_addralign : 1;
            total_size = (total_size + align - 1) & ~(align - 1);
            total_size += shdr[i].sh_size;
        }
    }
    if (total_size == 0) total_size = 0x1000;

    size_t plt_offset = (total_size + 15) & ~15;
    total_size = plt_offset + 0x1000; // Extra page for PLT trampolines

    // Allocate memory
    uint8_t *module_base = (uint8_t *)kmalloc(total_size + 0x1000);
    if (!module_base) {
        kfree(section_addrs);
        klog(LOG_ERROR, "LKM: Failed to allocate memory for module.");
        return -1;
    }

    uint8_t *aligned_base = (uint8_t *)(((uint64_t)module_base + 0xFFF) & ~0xFFF);
    memset(aligned_base, 0, total_size);

    // Lay out sections
    size_t current_offset = 0;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_flags & 0x2 /* SHF_ALLOC */) {
            size_t align = shdr[i].sh_addralign ? shdr[i].sh_addralign : 1;
            current_offset = (current_offset + align - 1) & ~(align - 1);
            section_addrs[i] = (uint64_t)(aligned_base + current_offset);
            if (shdr[i].sh_type != SHT_NOBITS) {
                memcpy(aligned_base + current_offset, data + shdr[i].sh_offset, shdr[i].sh_size);
            }
            current_offset += shdr[i].sh_size;
        }
    }

    #define MAX_PLT_TRAMPOLINES 64
    typedef struct {
        uint64_t target;
        uint64_t tramp_addr;
    } plt_slot_t;
    plt_slot_t plt_slots[MAX_PLT_TRAMPOLINES];
    int plt_slot_count = 0;

    // Process relocations (.rela sections)
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_RELA) {
            uint32_t target_sec = shdr[i].sh_info;
            if (target_sec >= ehdr->e_shnum || !section_addrs[target_sec]) continue;
            uint8_t *target_base = (uint8_t *)section_addrs[target_sec];

            const Elf64_Rela *rela = (const Elf64_Rela *)(data + shdr[i].sh_offset);
            int rela_count = shdr[i].sh_size / sizeof(Elf64_Rela);

            const Elf64_Shdr *symsec = &shdr[shdr[i].sh_link];
            const Elf64_Sym *syms = (const Elf64_Sym *)(data + symsec->sh_offset);
            const char *sym_strs = (const char *)(data + shdr[symsec->sh_link].sh_offset);

            for (int r = 0; r < rela_count; r++) {
                uint32_t sym_idx = ELF64_R_SYM(rela[r].r_info);
                uint32_t type = ELF64_R_TYPE(rela[r].r_info);
                const Elf64_Sym *sym = &syms[sym_idx];

                uint64_t S = 0;
                if (sym->st_shndx == 0 /* SHN_UNDEF */) {
                    const char *sname = sym_strs + sym->st_name;
                    S = find_kernel_symbol(sname);
                    if (!S) {
                        klog(LOG_WARN, "LKM: Unresolved symbol '%s'", sname);
                        continue;
                    }
                } else if (sym->st_shndx < ehdr->e_shnum) {
                    S = section_addrs[sym->st_shndx] + sym->st_value;
                }

                uint8_t *P = target_base + rela[r].r_offset;
                int64_t A = rela[r].r_addend;

                if (type == 1 /* R_X86_64_64 */) {
                    *(uint64_t *)P = (uint64_t)(S + A);
                } else if (type == 2 /* R_X86_64_PC32 */ || type == 4 /* R_X86_64_PLT32 */) {
                    int64_t disp = (int64_t)S + A - (int64_t)P;
                    if (disp < -2147483648LL || disp > 2147483647LL) {
                        // Relocation displacement exceeds 32 bits. Route through 64-bit PLT trampoline!
                        uint64_t target = (uint64_t)(S + A);
                        uint64_t tramp_addr = 0;
                        for (int s = 0; s < plt_slot_count; s++) {
                            if (plt_slots[s].target == target) {
                                tramp_addr = plt_slots[s].tramp_addr;
                                break;
                            }
                        }
                        if (!tramp_addr && plt_slot_count < MAX_PLT_TRAMPOLINES) {
                            uint8_t *tramp = aligned_base + plt_offset + (plt_slot_count * 16);
                            // Instruction: jmp qword ptr [rip + 0]
                            tramp[0] = 0xFF;
                            tramp[1] = 0x25;
                            tramp[2] = 0x00;
                            tramp[3] = 0x00;
                            tramp[4] = 0x00;
                            tramp[5] = 0x00;
                            *(uint64_t *)(tramp + 6) = target;
                            tramp[14] = 0x90;
                            tramp[15] = 0x90;

                            tramp_addr = (uint64_t)tramp;
                            plt_slots[plt_slot_count].target = target;
                            plt_slots[plt_slot_count].tramp_addr = tramp_addr;
                            plt_slot_count++;
                        }
                        if (tramp_addr) {
                            disp = (int64_t)tramp_addr - (int64_t)P;
                        }
                    }
                    *(int32_t *)P = (int32_t)disp;
                } else if (type == 10 /* R_X86_64_32 */) {
                    *(uint32_t *)P = (uint32_t)(S + A);
                } else if (type == 11 /* R_X86_64_32S */) {
                    *(int32_t *)P = (int32_t)(S + A);
                }
            }
        }
    }

    // Locate module_init and module_exit symbols
    lkm_init_t init_func = NULL;
    lkm_exit_t exit_func = NULL;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            const Elf64_Sym *syms = (const Elf64_Sym *)(data + shdr[i].sh_offset);
            const char *sym_strs = (const char *)(data + shdr[shdr[i].sh_link].sh_offset);
            int count = shdr[i].sh_size / sizeof(Elf64_Sym);
            for (int s = 0; s < count; s++) {
                const char *sname = sym_strs + syms[s].st_name;
                if (syms[s].st_shndx < ehdr->e_shnum && section_addrs[syms[s].st_shndx]) {
                    uint64_t sym_addr = section_addrs[syms[s].st_shndx] + syms[s].st_value;
                    if (strcmp(sname, "__lkm_init_ptr") == 0) {
                        init_func = *(lkm_init_t *)sym_addr;
                    } else if (strcmp(sname, "module_init") == 0) {
                        if (!init_func) init_func = (lkm_init_t)sym_addr;
                    } else if (strcmp(sname, "__lkm_exit_ptr") == 0) {
                        exit_func = *(lkm_exit_t *)sym_addr;
                    } else if (strcmp(sname, "module_exit") == 0) {
                        if (!exit_func) exit_func = (lkm_exit_t)sym_addr;
                    }
                }
            }
        }
    }

    kfree(section_addrs);

    // Create module structure
    kernel_module_t *mod = (kernel_module_t *)kmalloc(sizeof(kernel_module_t));
    if (!mod) {
        kfree(module_base);
        klog(LOG_ERROR, "LKM: Failed to allocate module structure.");
        return -1;
    }

    memset(mod, 0, sizeof(kernel_module_t));
    strncpy(mod->name, "module", sizeof(mod->name) - 1);
    mod->base = aligned_base;
    mod->size = total_size;
    mod->init = init_func;
    mod->exit = exit_func;
    mod->refcount = 1;

    klog(LOG_INFO, "LKM: Module loaded at %p (init=%p, exit=%p).", mod->base, mod->init, mod->exit);

    // Call init function
    if (mod->init) {
        int result = mod->init();
        if (result != 0) {
            klog(LOG_ERROR, "LKM: Module init failed with code %d.", result);
            kfree(mod);
            kfree(module_base);
            return -1;
        }
        klog(LOG_INFO, "LKM: Module init returned success.");
    }

    *out_module = mod;
    return 0;
}

int lkm_load_module(lkm_manager_t *mgr, const char *path) {
    klog(LOG_INFO, "LKM: Loading module from '%s'.", path);

    // Extract basename for module name
    const char *bname = strrchr(path, '/');
    bname = bname ? bname + 1 : path;

    // Check if already loaded
    if (lkm_find_module(mgr, bname)) {
        klog(LOG_WARN, "LKM: Module '%s' already loaded.", bname);
        return -1;
    }

    vfs_node_t *node = vfs_resolve_path(vfs_root, path);
    if (!node) {
        // Try /boot/modules or /modules prefix
        char alt_path[MAX_FILENAME_LEN];
        ksprintf(alt_path, "/boot/modules/%s", bname);
        node = vfs_resolve_path(vfs_root, alt_path);
        if (!node) {
            ksprintf(alt_path, "/modules/%s", bname);
            node = vfs_resolve_path(vfs_root, alt_path);
        }
    }
    if (!node) {
        klog(LOG_ERROR, "LKM: Module file not found: %s", path);
        return -1;
    }

    if (node->length == 0) {
        klog(LOG_ERROR, "LKM: Module file is empty.");
        return -1;
    }

    uint8_t *buffer = (uint8_t *)kmalloc(node->length);
    if (!buffer) {
        klog(LOG_ERROR, "LKM: Failed to allocate buffer for module.");
        return -1;
    }

    uint32_t bytes_read = vfs_read(node, 0, node->length, buffer);
    if (bytes_read != node->length) {
        klog(LOG_ERROR, "LKM: Failed to read module file.");
        kfree(buffer);
        return -1;
    }

    kernel_module_t *mod = NULL;
    int result = lkm_load_from_buffer(buffer, bytes_read, &mod);
    kfree(buffer);

    if (result != 0 || !mod) {
        return -1;
    }

    strncpy(mod->name, bname, sizeof(mod->name) - 1);

    // Strip .ko suffix if present
    char *dot = strrchr(mod->name, '.');
    if (dot && strcmp(dot, ".ko") == 0) {
        *dot = '\0';
    }

    mod->next = mgr->modules;
    mgr->modules = mod;
    mgr->module_count++;

    klog(LOG_INFO, "LKM: Module '%s' registered successfully.", mod->name);
    return 0;
}

void lkm_unload_module_internal(kernel_module_t *mod) {
    if (!mod) return;

    if (mod->exit) {
        klog(LOG_INFO, "LKM: Calling module exit function.");
        mod->exit();
    }

    if (mod->base) {
        kfree(mod->base);
    }
    kfree(mod);
}

int lkm_unload_module(lkm_manager_t *mgr, const char *name) {
    klog(LOG_INFO, "LKM: Unloading module '%s'.", name);

    kernel_module_t *mod = mgr->modules;
    kernel_module_t *prev = NULL;

    while (mod) {
        if (strcmp(mod->name, name) == 0) {
            if (mod->refcount > 1) {
                klog(LOG_ERROR, "LKM: Module '%s' in use (refcount=%d).", name, mod->refcount);
                return -1;
            }

            if (prev) {
                prev->next = mod->next;
            } else {
                mgr->modules = mod->next;
            }
            mgr->module_count--;

            lkm_unload_module_internal(mod);
            klog(LOG_INFO, "LKM: Module '%s' unloaded successfully.", name);
            return 0;
        }
        prev = mod;
        mod = mod->next;
    }

    klog(LOG_ERROR, "LKM: Module '%s' not found.", name);
    return -1;
}

void lkm_list_modules(lkm_manager_t *mgr) {
    klog_print_str("Loaded modules:\n", true);
    klog_print_str("----------------\n", true);

    if (!mgr->modules) {
        klog_print_str("  (none)\n", true);
        return;
    }

    kernel_module_t *mod = mgr->modules;
    while (mod) {
        char buf[128];
        ksprintf(buf, "  %-20s @ %p  [%d refs]\n", mod->name, mod->base, mod->refcount);
        klog_print_str(buf, true);
        mod = mod->next;
    }
}
