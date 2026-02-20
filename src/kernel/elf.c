#include "elf.h"
#include "heap.h"
#include "kstring.h"
#include "log.h"
#include "pmm.h"
#include "thread.h"
#include "vfs.h"
#include "vmm.h"


extern uint64_t hhdm_offset;
#define P_TO_V(p) ((void *)((uint64_t)(p) + hhdm_offset))

elf_load_result_t elf_load(pml4_t *pml4, const uint8_t *elf_data) {
  elf_load_result_t result = {0, 0};
  klog(LOG_INFO, "ELF: elf_load entered with data at %x", elf_data);
  if (!elf_data) {
    klog(LOG_ERROR, "ELF: elf_load called with NULL data.");
    return result;
  }

  const Elf64_Ehdr *header = (const Elf64_Ehdr *)elf_data;

  // 1. Validate ELF Header
  if (header->e_ident[EI_MAG0] != ELFMAG0 ||
      header->e_ident[EI_MAG1] != ELFMAG1 ||
      header->e_ident[EI_MAG2] != ELFMAG2 ||
      header->e_ident[EI_MAG3] != ELFMAG3) {
    klog(LOG_ERROR, "ELF: Invalid magic number.");
    return result; // Failure
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

  // 2. Iterate through Program Headers
  for (int i = 0; i < header->e_phnum; i++) {
    const Elf64_Phdr *p_header =
        (const Elf64_Phdr *)(elf_data + header->e_phoff +
                             (i * header->e_phentsize));

    if (p_header->p_type == PT_LOAD) {
      klog(LOG_INFO,
           "ELF: PT_LOAD segment %d: vaddr=%x, memsz=%x, file_offset=%x", i,
           p_header->p_vaddr, p_header->p_memsz, p_header->p_offset);

      uint64_t vaddr = p_header->p_vaddr;
      uint64_t mem_size = p_header->p_memsz;
      uint64_t file_size = p_header->p_filesz;
      uint64_t page_flags = PAGE_PRESENT | PAGE_USER;

      if (p_header->p_flags & PF_W) {
        page_flags |= PAGE_WRITE;
      }
      if (!(p_header->p_flags & PF_X)) { // If segment is not executable
        page_flags |= PAGE_NO_EXEC;
      }

      for (uint64_t j = 0; j < mem_size; j += PAGE_SIZE) {
        void *phys_page = pmm_alloc_page();
        if (!phys_page) {
          panic("ELF: Out of physical memory to load segment.", NULL);
        }
        memset(P_TO_V(phys_page), 0, PAGE_SIZE); // Zero-initialize the page!
        vmm_map_page(pml4, (void *)(vaddr + j), phys_page, page_flags);

        // Copy data if needed for this page
        uint64_t offset_in_file = p_header->p_offset + j;
        if (offset_in_file < p_header->p_offset + file_size) {
          uint64_t to_copy = PAGE_SIZE;
          if (p_header->p_offset + file_size - offset_in_file < to_copy) {
            to_copy = p_header->p_offset + file_size - offset_in_file;
          }
          void *src = (void *)(elf_data + offset_in_file);
          klog(LOG_DEBUG, "ELF: Copying %d bytes from %x to phys %x", to_copy,
               src, phys_page);
          // Use HHDM to access physical memory from kernel space
          memcpy(P_TO_V(phys_page), src, to_copy);
        }
      }
      if (vaddr + mem_size > max_vaddr) {
        max_vaddr = vaddr + mem_size;
      }
    }
  }

  klog(LOG_INFO, "ELF: Segments loaded.");
  result.entry_point = header->e_entry;
  result.program_break = max_vaddr;
  return result;
}

int elf_exec_as_thread(const char *path, int argc, char *argv[]) {
  klog(LOG_INFO, "elf_exec_as_thread: entered with path '%s'", path);
  klog(LOG_INFO, "ELF Exec: Loading %s", path);
  vfs_node_t *node = vfs_resolve_path(vfs_root, path);
  if (!node) {
    klog(LOG_ERROR, "ELF Exec: File not found");
    return -1;
  }

  klog(LOG_INFO, "ELF Exec: File found, size %d bytes", node->length);

  uint8_t *buffer = kmalloc(node->length);
  if (!buffer) {
    klog(LOG_ERROR, "ELF Exec: Could not allocate buffer for file");
    return -1;
  }

  klog(LOG_INFO, "ELF Exec: Buffer allocated at %x", buffer);

  vfs_read(node, 0, node->length, buffer);

  klog(LOG_INFO, "ELF Exec: Calling vmm_create_address_space");
  pml4_t *new_pml4 = vmm_create_address_space();
  klog(LOG_INFO, "ELF Exec: vmm_create_address_space returned");
  if (!new_pml4) {
    klog(LOG_ERROR, "ELF Exec: Could not create new address space");
    kfree(buffer);
    return -1;
  }

  elf_load_result_t result = elf_load(new_pml4, buffer);
  kfree(buffer);

  if (result.entry_point) {
    klog(LOG_INFO, "ELF Exec: Starting userspace thread at entry point %x",
         result.entry_point);
    thread_create_userspace(result.entry_point, new_pml4, result.program_break,
                            argc, argv);
    return 0; // Success
  }

  klog(LOG_ERROR, "ELF Exec: Failed to load ELF");
  vmm_destroy_address_space(new_pml4);
  return -1; // Failure
}