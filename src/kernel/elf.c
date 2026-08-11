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


static void *elf_get_mapping(pml4_t *pml4, uint64_t vaddr) {
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
    return (void *)(pt[pti] & ~0xFFFULL);
}

elf_load_result_t elf_load(pml4_t *pml4, const uint8_t *elf_data) {
    elf_load_result_t result = {0};
    klog(LOG_INFO, "ELF: elf_load entered with data at %x", elf_data);

    if (!elf_data) {
        klog(LOG_ERROR, "ELF: elf_load called with NULL data.");
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

    klog(LOG_INFO, "ELF: Valid header found. Loading segments...");

    uint64_t max_vaddr = 0;

    for (int i = 0; i < header->e_phnum; i++) {
        const Elf64_Phdr *p_header =
            (const Elf64_Phdr *)(elf_data + header->e_phoff +
                                 (i * header->e_phentsize));

        if (p_header->p_type != PT_LOAD)
            continue;

        if (p_header->p_memsz == 0)
            continue;

        klog(LOG_INFO,
             "ELF: PT_LOAD segment %d: vaddr=%x, memsz=%x, file_offset=%x", i,
             p_header->p_vaddr, p_header->p_memsz, p_header->p_offset);

        uint64_t vaddr     = p_header->p_vaddr;
        uint64_t mem_size  = p_header->p_memsz;
        uint64_t file_size = p_header->p_filesz;
        uint64_t file_off  = p_header->p_offset;

        uint64_t page_flags = PAGE_PRESENT | PAGE_USER;
        if (p_header->p_flags & PF_W)
            page_flags |= PAGE_WRITE;
        if (!(p_header->p_flags & PF_X))
            page_flags |= PAGE_NO_EXEC;


        uint64_t vstart = vaddr & ~0xFFFULL;
        uint64_t vend   = (vaddr + mem_size + 0xFFF) & ~0xFFFULL;

        for (uint64_t va = vstart; va < vend; va += PAGE_SIZE) {
            void *phys = elf_get_mapping(pml4, va);

            if (!phys) {
                phys = pmm_alloc_page();
                if (!phys)
                    panic("ELF: Out of physical memory to load segment.", NULL);
                memset(P_TO_V(phys), 0, PAGE_SIZE);
                vmm_map_page(pml4, (void *)va, phys, page_flags);
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

int elf_exec_as_thread(const char *path, int argc, char *argv[]) {
    klog(LOG_INFO, "elf_exec_as_thread: entered with path '%s'", path);

    vfs_node_t *node = vfs_resolve_path(vfs_root, path);
    if (!node) {
        klog(LOG_ERROR, "ELF Exec: File not found");
        return -1;
    }

    uint8_t *buffer = kmalloc(node->length);
    if (!buffer) {
        klog(LOG_ERROR, "ELF Exec: Could not allocate buffer for file");
        return -1;
    }

    vfs_read(node, 0, node->length, buffer);

    pml4_t *new_pml4 = vmm_create_address_space();
    if (!new_pml4) {
        klog(LOG_ERROR, "ELF Exec: Could not create new address space");
        kfree(buffer);
        return -1;
    }

    elf_load_result_t result = elf_load(new_pml4, buffer);
    kfree(buffer);

    if (result.success) {
        klog(LOG_INFO, "ELF Exec: Starting userspace thread at entry point %x",
             result.entry_point);
        thread_create_userspace(result.entry_point, new_pml4, result.program_break,
                                argc, argv);
        return 0;
    }

    klog(LOG_ERROR, "ELF Exec: Failed to load ELF");
    vmm_destroy_address_space(new_pml4);
    return -1;
}