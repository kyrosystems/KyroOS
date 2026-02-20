#include "lkm.h"
#include "elf.h" // Re-use ELF header definitions
#include "crypto.h"
#include "log.h"
#include "heap.h"
#include <string.h>

#define KROSMODL_MAGIC 0x4B524F534D4F444C

// This is a simplified LKM loader. A real one would be much more complex,
// especially the ELF relocation part.

int lkm_load(const uint8_t* module_data, size_t module_size) {
    klog(LOG_INFO, "LKM: Attempting to load module.");

    // 1. Verify signature (stubbed)
    if (module_size <= 136) {
        klog(LOG_ERROR, "LKM: Module too small to be signed.");
        return -1;
    }
    
    uint64_t magic = *(uint64_t*)(module_data + module_size - 8);
    if (magic != KROSMODL_MAGIC) {
        klog(LOG_ERROR, "LKM: Invalid module magic number. Not a signed KyroOS module.");
        return -1;
    }

    const uint8_t* signature = module_data + module_size - 136;
    size_t elf_size = module_size - 136;
    uint8_t hash[SHA256_HASH_SIZE];
    
    sha256_hash(module_data, elf_size, hash);
    if (!rsa_verify_signature(signature, hash)) {
        klog(LOG_ERROR, "LKM: Signature verification failed.");
        return -1;
    }
    klog(LOG_INFO, "LKM: Signature verified.");

    // 2. Load ELF relocatable object
    const Elf64_Ehdr* header = (const Elf64_Ehdr*)module_data;
    if (header->e_ident[EI_CLASS] != ELFCLASS64 || header->e_type != ET_REL) {
        klog(LOG_ERROR, "LKM: Not a 64-bit relocatable ELF object.");
        return -1;
    }
    
    // In a real loader, we would:
    // - Allocate executable memory for .text, .data, .rodata sections.
    // - Copy section data into the allocated memory.
    // - Process relocation entries (.rela.text, .rela.data) to fix up addresses.
    //   This involves finding symbols in the kernel's symbol table.
    // - For this simplified version, we assume the module is simple enough not
    //   to need complex relocations and we will just find the init function.
    
    klog(LOG_WARN, "LKM: ELF relocation is STUBBED. Only simple modules will work.");
    
    // Find and call init function (a real loader would use symbol tables)
    // For this demo, we assume a simple module structure and search for the symbol.
    // This is NOT a robust way to do this.

    // logging success.
    
    klog(LOG_INFO, "LKM: Module loaded (stub). Would call init function now.");
    
    // In a real implementation:
    // lkm_init_t init_func = (lkm_init_t) find_symbol(module, "__lkm_init_ptr");
    // if (init_func) {
    //     int result = (*init_func)();
    //     if (result != 0) { ... handle failure ... }
    // }
    
    return 0; // Success
}
