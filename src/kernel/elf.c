#include "elf.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "pmm.h"
#include "thread.h"
#include "vfs.h"
#include "vmm.h"

extern uint64_t kernel_hhdm_offset;
#define P_TO_V(p) ((void *)((uint64_t)(p) + kernel_hhdm_offset))


#define ELF_USERSPACE_END 0x0000800000000000ULL
#define ELF_SHEBANG_MAX_DEPTH 4

static uint64_t *elf_get_pte(pml4_t *pml4, uint64_t vaddr) {
    uint64_t *table = (uint64_t *)pml4;

    uint64_t pml4i = (vaddr >> 39) & 0x1FF;
    uint64_t pdpti = (vaddr >> 30) & 0x1FF;
    uint64_t pdi   = (vaddr >> 21) & 0x1FF;
    uint64_t pti   = (vaddr >> 12) & 0x1FF;

    if (!(table[pml4i] & PAGE_PRESENT)) return NULL;
    uint64_t *pdpt = (uint64_t *)P_TO_V(table[pml4i] & ~0xFFFULL);

    if (!(pdpt[pdpti] & PAGE_PRESENT)) return NULL;
    uint64_t *pd = (uint64_t *)P_TO_V(pdpt[pdpti] & ~0xFFFULL);

    if (!(pd[pdi] & PAGE_PRESENT)) return NULL;
    uint64_t *pt = (uint64_t *)P_TO_V(pd[pdi] & ~0xFFFULL);

    if (!(pt[pti] & PAGE_PRESENT)) return NULL;
    return &pt[pti];
}

elf_load_result_t elf_load_sized(pml4_t *pml4, const uint8_t *elf_data, size_t elf_size) {
    elf_load_result_t result = {0};
    klog(LOG_INFO, "ELF: elf_load entered with data at %x", elf_data);

    if (!pml4 || !elf_data || elf_size < sizeof(Elf64_Ehdr)) {
        klog(LOG_ERROR, "ELF: Invalid arguments or truncated header.");
        return result;
    }

    const Elf64_Ehdr *header = (const Elf64_Ehdr *)elf_data;

    if (header->e_ident[EI_MAG0] != ELFMAG0 ||
        header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 ||
        header->e_ident[EI_MAG3] != ELFMAG3) {
        klog(LOG_ERROR, "ELF: Invalid magic number.");
        return result;
    }
    if (header->e_ident[EI_CLASS] != ELFCLASS64) {
        klog(LOG_ERROR, "ELF: Not a 64-bit executable.");
        return result;
    }
    if (header->e_type != ET_EXEC) {
        klog(LOG_ERROR, "ELF: Not an executable file.");
        return result;
    }
    if (header->e_entry >= ELF_USERSPACE_END) {
        klog(LOG_ERROR, "ELF: Entry point outside userspace.");
        return result;
    }
    if (header->e_phentsize < sizeof(Elf64_Phdr)) {
        klog(LOG_ERROR, "ELF: Invalid program header entry size.");
        return result;
    }
    uint64_t ph_table_size = (uint64_t)header->e_phnum * (uint64_t)header->e_phentsize;
    if (header->e_phoff > elf_size || ph_table_size > elf_size - header->e_phoff) {
        klog(LOG_ERROR, "ELF: Program header table out of bounds.");
        return result;
    }

    klog(LOG_INFO, "ELF: Valid header found. Loading segments...");

    uint64_t max_vaddr = 0;

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        const Elf64_Phdr *p_header =
            (const Elf64_Phdr *)(elf_data + header->e_phoff +
                                 ((uint64_t)i * header->e_phentsize));

        if (p_header->p_type != PT_LOAD)
            continue;

        if (p_header->p_memsz == 0)
            continue;

        uint64_t vaddr     = p_header->p_vaddr;
        uint64_t mem_size  = p_header->p_memsz;
        uint64_t file_size = p_header->p_filesz;
        uint64_t file_off  = p_header->p_offset;

        if (file_size > mem_size) {
            klog(LOG_ERROR, "ELF: Segment filesz exceeds memsz.");
            return result;
        }
        if (file_off > elf_size || file_size > elf_size - file_off) {
            klog(LOG_ERROR, "ELF: Segment file range out of bounds.");
            return result;
        }
        if (vaddr >= ELF_USERSPACE_END || mem_size > ELF_USERSPACE_END - vaddr) {
            klog(LOG_ERROR, "ELF: Segment virtual range out of userspace bounds.");
            return result;
        }

        klog(LOG_INFO,
             "ELF: PT_LOAD segment %d: vaddr=%x, memsz=%x, file_offset=%x", i,
             vaddr, mem_size, file_off);

        uint64_t page_flags = PAGE_PRESENT | PAGE_USER;
        if (p_header->p_flags & PF_W)
            page_flags |= PAGE_WRITE;
        if (!(p_header->p_flags & PF_X) && vmm_nx_supported)
            page_flags |= PAGE_NO_EXEC;

        uint64_t vstart = vaddr & ~0xFFFULL;
        uint64_t vend   = (vaddr + mem_size + 0xFFF) & ~0xFFFULL;

        for (uint64_t va = vstart; va < vend; va += PAGE_SIZE) {
            uint64_t *pte = elf_get_pte(pml4, va);
            void *phys = pte ? (void *)(*pte & ~0xFFFULL) : NULL;

            if (!phys) {
                phys = pmm_alloc_page();
                if (!phys) {
                    klog(LOG_ERROR, "ELF: Out of physical memory to load segment.");
                    return result;
                }
                memset(P_TO_V(phys), 0, PAGE_SIZE);
                vmm_map_page(pml4, (void *)va, phys, page_flags);
            } else {
                // Upgrade permissions if overlapping segments share a page
                uint64_t old_flags = *pte & ~PHYSICAL_ADDR_MASK;
                uint64_t new_flags = old_flags | PAGE_PRESENT | PAGE_USER;
                if (page_flags & PAGE_WRITE)
                    new_flags |= PAGE_WRITE;
                if (!(page_flags & PAGE_NO_EXEC))
                    new_flags &= ~PAGE_NO_EXEC;
                *pte = (uint64_t)phys | new_flags;
            }

            uint64_t file_vend  = vaddr + file_size;
            uint64_t page_vend  = va + PAGE_SIZE;
            uint64_t copy_start = va > vaddr ? va : vaddr;
            uint64_t copy_end   = page_vend < file_vend ? page_vend : file_vend;

            if (copy_start < copy_end) {
                uint64_t src_off = file_off + (copy_start - vaddr);
                memcpy((uint8_t *)P_TO_V(phys) + (copy_start - va),
                       (const void *)(elf_data + src_off),
                       copy_end - copy_start);
            }
        }

        if (vend > max_vaddr)
            max_vaddr = vend;
    }

    klog(LOG_INFO, "ELF: Segments loaded.");
    result.entry_point   = header->e_entry;
    result.program_break = max_vaddr;
    result.success       = 1;
    return result;
}

elf_load_result_t elf_load(pml4_t *pml4, const uint8_t *elf_data) {
    return elf_load_sized(pml4, elf_data, (size_t)-1);
}

int elf_exec_as_thread(const char *path, int argc, char *argv[]) {
    return elf_exec_as_thread_handle(path, argc, argv) ? 0 : -1;
}

static struct thread *elf_exec_as_thread_handle_depth(const char *path, int argc, char *argv[], int depth) {
    if (!path || argc < 0 || argc > 64) {
        return NULL;
    }
    klog(LOG_INFO, "elf_exec_as_thread: entered with path '%s'", path);

    vfs_node_t *node = vfs_resolve_path(vfs_root, path);
    if (!node || (node->flags & VFS_DIRECTORY) || node->length == 0) {
        klog(LOG_ERROR, "ELF Exec: File not found or invalid");
        return NULL;
    }

    uint32_t file_len = node->length;
    uint8_t *buffer = kmalloc(file_len);
    if (!buffer) {
        klog(LOG_ERROR, "ELF Exec: Could not allocate buffer for file");
        return NULL;
    }

    if (vfs_read(node, 0, file_len, buffer) != file_len) {
        klog(LOG_ERROR, "ELF Exec: Short read from file");
        kfree(buffer);
        return NULL;
    }

    // Shebang (#!...) interpreter support for scripts
    if (file_len >= 2 && buffer[0] == '#' && buffer[1] == '!') {
        if (depth >= ELF_SHEBANG_MAX_DEPTH) {
            klog(LOG_ERROR, "ELF Exec: Shebang recursion depth exceeded");
            kfree(buffer);
            return NULL;
        }
        char interp[128];
        int idx = 0;
        uint32_t p = 2;
        while (p < file_len && (buffer[p] == ' ' || buffer[p] == '\t')) p++;
        while (p < file_len && buffer[p] != '\n' && buffer[p] != '\r' && buffer[p] != ' ' && idx < (int)sizeof(interp) - 1) {
            interp[idx++] = (char)buffer[p++];
        }
        interp[idx] = '\0';
        kfree(buffer);

        if (interp[0] == '\0') {
            strncpy(interp, "/bin/sh", sizeof(interp) - 1);
            interp[sizeof(interp) - 1] = '\0';
        }

        int new_argc = argc + 1;
        if (new_argc > 64) {
            klog(LOG_ERROR, "ELF Exec: Too many arguments after shebang");
            return NULL;
        }
        char **new_argv = (char **)kmalloc((size_t)(new_argc + 1) * sizeof(char *));
        if (!new_argv) return NULL;
        new_argv[0] = interp;
        new_argv[1] = (char *)path;
        for (int i = 1; i < argc; i++) {
            new_argv[i + 1] = argv[i];
        }
        new_argv[new_argc] = NULL;

        struct thread *th = elf_exec_as_thread_handle_depth(interp, new_argc, new_argv, depth + 1);
        kfree(new_argv);
        return th;
    }

    pml4_t *new_pml4 = vmm_create_address_space();
    if (!new_pml4) {
        klog(LOG_ERROR, "ELF Exec: Could not create new address space");
        kfree(buffer);
        return NULL;
    }

    elf_load_result_t result = elf_load_sized(new_pml4, buffer, file_len);
    kfree(buffer);

    if (result.success) {
        klog(LOG_INFO, "ELF Exec: Starting userspace thread at entry point %x",
             result.entry_point);
        return thread_create_userspace(result.entry_point, new_pml4,
                                       result.program_break,
                                       argc, argv, path);
    }

    klog(LOG_ERROR, "ELF Exec: Failed to load ELF");
    vmm_destroy_address_space(new_pml4);
    return NULL;
}

struct thread *elf_exec_as_thread_handle(const char *path, int argc, char *argv[]) {
    return elf_exec_as_thread_handle_depth(path, argc, argv, 0);
}