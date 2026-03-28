#include "lkm.h"
#include "elf.h"
#include "log.h"
#include "heap.h"
#include "kstring.h"
#include "pmm.h"
#include "vmm.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Module initialization function type
typedef int (*lkm_init_t)(void);
// Module exit function type
typedef void (*lkm_exit_t)(void);

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
    // Logging
    {"klog", (uint64_t)klog},
    {"klog_print_str", (uint64_t)klog_print_str},
    {"klog_putchar", (uint64_t)klog_putchar},
    
    // Memory
    {"kmalloc", (uint64_t)kmalloc},
    {"kfree", (uint64_t)kfree},
    
    // String
    {"memcpy", (uint64_t)memcpy},
    {"memset", (uint64_t)memset},
    {"strlen", (uint64_t)strlen},
    {"strcmp", (uint64_t)strcmp},
    {"strncpy", (uint64_t)strncpy},
    
    // VFS
    {"vfs_read", (uint64_t)vfs_read},
    {"vfs_write", (uint64_t)vfs_write},
    {"vfs_finddir", (uint64_t)vfs_finddir},
    {"vfs_resolve_path", (uint64_t)vfs_resolve_path},
    
    // Scheduler
    {"schedule", (uint64_t)schedule},
    {"thread_create", (uint64_t)thread_create},
    
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

void lkm_manager_init(lkm_manager_t *mgr) {
    mgr->modules = NULL;
    mgr->module_count = 0;
    klog(LOG_INFO, "LKM: Module manager initialized.");
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

// Find ELF section by name
static const Elf64_Shdr *find_section_by_name(const uint8_t *base, const Elf64_Ehdr *ehdr, const char *name) {
    const Elf64_Shdr *shdr = (const Elf64_Shdr *)(base + ehdr->e_shoff);
    const char *shstrtab = (const char *)(base + shdr[ehdr->e_shstrndx].sh_offset);
    
    for (int i = 0; i < ehdr->e_shnum; i++) {
        if (strcmp(shstrtab + shdr[i].sh_name, name) == 0) {
            return &shdr[i];
        }
    }
    return NULL;
}

// Find symbol in symbol table
static const Elf64_Sym *find_symbol_by_name(const uint8_t *base, const Elf64_Shdr *symtab, 
                                             const char *shstrtab, const char *name) {
    const Elf64_Sym *sym = (const Elf64_Sym *)(base + symtab->sh_offset);
    int sym_count = symtab->sh_size / sizeof(Elf64_Sym);
    
    for (int i = 0; i < sym_count; i++) {
        if (sym[i].st_name != 0) {
            const char *sym_name = shstrtab + sym[i].st_name;
            if (strcmp(sym_name, name) == 0) {
                return &sym[i];
            }
        }
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
    
    // Find sections
    const Elf64_Shdr *symtab = find_section_by_name(data, ehdr, ".symtab");
    const Elf64_Shdr *strtab = find_section_by_name(data, ehdr, ".strtab");
    const Elf64_Shdr *shstrtab_hdr = find_section_by_name(data, ehdr, ".shstrtab");
    const Elf64_Shdr *text = find_section_by_name(data, ehdr, ".text");
    const Elf64_Shdr *data_sec = find_section_by_name(data, ehdr, ".data");
    const Elf64_Shdr *rodata = find_section_by_name(data, ehdr, ".rodata");
    const Elf64_Shdr *bss = find_section_by_name(data, ehdr, ".bss");
    
    if (!text) {
        klog(LOG_ERROR, "LKM: No .text section found.");
        return -1;
    }
    
    const char *shstrtab = (const char *)(data + shstrtab_hdr->sh_offset);
    
    // Calculate total memory needed
    size_t total_size = 0;
    if (text) total_size = (total_size > text->sh_addr + text->sh_size) ? 
                           (total_size) : (text->sh_addr + text->sh_size);
    if (data_sec) total_size = (total_size > data_sec->sh_addr + data_sec->sh_size) ? 
                                 (total_size) : (data_sec->sh_addr + data_sec->sh_size);
    if (rodata) total_size = (total_size > rodata->sh_addr + rodata->sh_size) ? 
                               (total_size) : (rodata->sh_addr + rodata->sh_size);
    if (bss) total_size = (total_size > bss->sh_addr + bss->sh_size) ? 
                            (total_size) : (bss->sh_addr + bss->sh_size);
    
    // Allocate executable memory
    uint8_t *module_base = (uint8_t *)kmalloc(total_size + 0x1000); // Extra for alignment
    if (!module_base) {
        klog(LOG_ERROR, "LKM: Failed to allocate memory for module.");
        return -1;
    }
    
    // Align to page boundary
    uint8_t *aligned_base = (uint8_t *)(((uint64_t)module_base + 0xFFF) & ~0xFFF);
    
    klog(LOG_INFO, "LKM: Allocated %u bytes at %p.", total_size, aligned_base);
    
    // Copy sections
    if (text) {
        memcpy(aligned_base + text->sh_addr, data + text->sh_offset, text->sh_size);
        klog(LOG_INFO, "LKM: Copied .text section (%u bytes).", text->sh_size);
    }
    if (data_sec) {
        memcpy(aligned_base + data_sec->sh_addr, data + data_sec->sh_offset, data_sec->sh_size);
    }
    if (rodata) {
        memcpy(aligned_base + rodata->sh_addr, data + rodata->sh_offset, rodata->sh_size);
    }
    
    // Process relocations
    const Elf64_Shdr *rela_text = find_section_by_name(data, ehdr, ".rela.text");
    const Elf64_Shdr *rela_data = find_section_by_name(data, ehdr, ".rela.data");
    
    if (symtab && strtab) {
        const char *sym_strtab = (const char *)(data + strtab->sh_offset);
        
        // Process .rela.text
        if (rela_text) {
            const Elf64_Rela *rela = (const Elf64_Rela *)(data + rela_text->sh_offset);
            int rela_count = rela_text->sh_size / sizeof(Elf64_Rela);
            
            for (int i = 0; i < rela_count; i++) {
                uint32_t sym_idx = ELF64_R_SYM(rela[i].r_info);
                uint32_t type = ELF64_R_TYPE(rela[i].r_info);
                
                if (sym_idx > 0 && sym_idx < (symtab->sh_size / sizeof(Elf64_Sym))) {
                    const Elf64_Sym *sym = (const Elf64_Sym *)(data + symtab->sh_offset) + sym_idx;
                    const char *sym_name = sym_strtab + sym->st_name;
                    
                    uint64_t sym_addr = 0;
                    
                    // Find symbol in kernel
                    sym_addr = find_kernel_symbol(sym_name);
                    
                    if (sym_addr == 0) {
                        // Try to find in module's own symbol table
                        if (sym->st_value != 0) {
                            sym_addr = (uint64_t)(aligned_base + sym->st_value);
                        }
                    }
                    
                    if (sym_addr != 0) {
                        // Apply relocation
                        uint64_t *reloc_addr = (uint64_t *)(aligned_base + rela[i].r_offset);
                        
                        if (type == R_X86_64_64) {
                            *reloc_addr = sym_addr + rela[i].r_addend;
                        } else if (type == R_X86_64_PC32) {
                            *reloc_addr = (int32_t)(sym_addr + rela[i].r_addend - rela[i].r_offset);
                        } else if (type == R_X86_64_32) {
                            *(uint32_t *)reloc_addr = (uint32_t)(sym_addr + rela[i].r_addend);
                        } else if (type == R_X86_64_32S) {
                            *(int32_t *)reloc_addr = (int32_t)(sym_addr + rela[i].r_addend);
                        }
                    } else {
                        klog(LOG_WARN, "LKM: Unresolved symbol '%s'.", sym_name);
                    }
                }
            }
            klog(LOG_INFO, "LKM: Processed %d relocations in .rela.text.", rela_count);
        }
        
        // Process .rela.data
        if (rela_data) {
            const Elf64_Rela *rela = (const Elf64_Rela *)(data + rela_data->sh_offset);
            int rela_count = rela_data->sh_size / sizeof(Elf64_Rela);
            
            for (int i = 0; i < rela_count; i++) {
                uint32_t sym_idx = ELF64_R_SYM(rela[i].r_info);
                uint32_t type = ELF64_R_TYPE(rela[i].r_info);
                
                if (sym_idx > 0 && sym_idx < (symtab->sh_size / sizeof(Elf64_Sym))) {
                    const Elf64_Sym *sym = (const Elf64_Sym *)(data + symtab->sh_offset) + sym_idx;
                    const char *sym_name = sym_strtab + sym->st_name;
                    
                    uint64_t sym_addr = find_kernel_symbol(sym_name);
                    
                    if (sym_addr == 0 && sym->st_value != 0) {
                        sym_addr = (uint64_t)(aligned_base + sym->st_value);
                    }
                    
                    if (sym_addr != 0) {
                        uint64_t *reloc_addr = (uint64_t *)(aligned_base + rela[i].r_offset);
                        
                        if (type == R_X86_64_64) {
                            *reloc_addr = sym_addr + rela[i].r_addend;
                        } else if (type == R_X86_64_PC32) {
                            *reloc_addr = (int32_t)(sym_addr + rela[i].r_addend - rela[i].r_offset);
                        } else if (type == R_X86_64_32) {
                            *(uint32_t *)reloc_addr = (uint32_t)(sym_addr + rela[i].r_addend);
                        }
                    }
                }
            }
            klog(LOG_INFO, "LKM: Processed %d relocations in .rela.data.", rela_count);
        }
    }
    
    // Find init and exit functions
    lkm_init_t init_func = NULL;
    lkm_exit_t exit_func = NULL;
    
    if (symtab && strtab) {
        const char *sym_strtab = (const char *)(data + strtab->sh_offset);
        
        const Elf64_Sym *init_sym = find_symbol_by_name(data, symtab, sym_strtab, "__lkm_init_ptr");
        if (!init_sym) {
            init_sym = find_symbol_by_name(data, symtab, sym_strtab, "module_init");
        }
        if (init_sym && init_sym->st_value != 0) {
            init_func = (lkm_init_t)(aligned_base + init_sym->st_value);
        }
        
        const Elf64_Sym *exit_sym = find_symbol_by_name(data, symtab, sym_strtab, "__lkm_exit_ptr");
        if (!exit_sym) {
            exit_sym = find_symbol_by_name(data, symtab, sym_strtab, "module_exit");
        }
        if (exit_sym && exit_sym->st_value != 0) {
            exit_func = (lkm_exit_t)(aligned_base + exit_sym->st_value);
        }
    }
    
    // Create module structure
    kernel_module_t *mod = (kernel_module_t *)kmalloc(sizeof(kernel_module_t));
    if (!mod) {
        kfree(module_base);
        klog(LOG_ERROR, "LKM: Failed to allocate module structure.");
        return -1;
    }
    
    memset(mod, 0, sizeof(kernel_module_t));
    strncpy(mod->name, "module", 63); // Default name
    mod->base = aligned_base;
    mod->size = total_size;
    mod->init = init_func;
    mod->exit = exit_func;
    mod->refcount = 1;
    
    // Try to get name from module data
    if (strtab) {
        const char *sym_strtab = (const char *)(data + strtab->sh_offset);
        const Elf64_Sym *name_sym = find_symbol_by_name(data, symtab, sym_strtab, "module_name");
        if (name_sym && name_sym->st_value != 0) {
            strncpy(mod->name, (const char *)(aligned_base + name_sym->st_value), 63);
        }
    }
    
    klog(LOG_INFO, "LKM: Module '%s' loaded at %p (init: %p, exit: %p).", 
         mod->name, mod->base, mod->init, mod->exit);
    
    // Call init function
    if (mod->init) {
        int result = mod->init();
        if (result != 0) {
            klog(LOG_ERROR, "LKM: Module init failed with code %d.", result);
            kfree(mod);
            kfree(module_base);
            return -1;
        }
        klog(LOG_INFO, "LKM: Module init succeeded.");
    }
    
    *out_module = mod;
    return 0;
}

int lkm_load_module(lkm_manager_t *mgr, const char *path) {
    klog(LOG_INFO, "LKM: Loading module from '%s'.", path);
    
    // Check if already loaded
    if (lkm_find_module(mgr, path)) {
        klog(LOG_WARN, "LKM: Module '%s' already loaded.", path);
        return -1;
    }
    
    // Resolve path
    vfs_node_t *node = vfs_resolve_path(vfs_root, path);
    if (!node) {
        klog(LOG_ERROR, "LKM: Module file not found: %s", path);
        return -1;
    }
    
    // Get file size
    if (node->length == 0) {
        klog(LOG_ERROR, "LKM: Module file is empty.");
        return -1;
    }
    
    // Allocate buffer
    uint8_t *buffer = (uint8_t *)kmalloc(node->length);
    if (!buffer) {
        klog(LOG_ERROR, "LKM: Failed to allocate buffer for module.");
        return -1;
    }
    
    // Read file
    uint32_t bytes_read = vfs_read(node, 0, node->length, buffer);
    if (bytes_read != node->length) {
        klog(LOG_ERROR, "LKM: Failed to read module file.");
        kfree(buffer);
        return -1;
    }
    
    // Load module from buffer
    kernel_module_t *mod;
    int result = lkm_load_from_buffer(buffer, bytes_read, &mod);
    kfree(buffer);
    
    if (result != 0) {
        return -1;
    }
    
    // Add to manager
    mod->next = mgr->modules;
    mgr->modules = mod;
    mgr->module_count++;
    
    klog(LOG_INFO, "LKM: Module '%s' loaded successfully.", mod->name);
    return 0;
}

void lkm_unload_module_internal(kernel_module_t *mod) {
    if (!mod) return;
    
    // Call exit function
    if (mod->exit) {
        klog(LOG_INFO, "LKM: Calling module exit function.");
        mod->exit();
    }
    
    // Free memory
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
                klog(LOG_ERROR, "LKM: Module '%s' is in use (refcount=%d).", name, mod->refcount);
                return -1;
            }
            
            if (prev) {
                prev->next = mod->next;
            } else {
                mgr->modules = mod->next;
            }
            mgr->module_count--;
            
            lkm_unload_module_internal(mod);
            klog(LOG_INFO, "LKM: Module '%s' unloaded.", name);
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
