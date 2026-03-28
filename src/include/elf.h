#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include "vmm.h" // For pml4_t

#define EI_NIDENT 16

// ELF Header
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} Elf64_Ehdr;

// Program Header
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

// e_ident[] indexes
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6

// e_ident[] values
#define ELFMAG0       0x7f
#define ELFMAG1       'E'
#define ELFMAG2       'L'
#define ELFMAG3       'F'
#define ELFCLASS64    2

// e_type values
#define ET_EXEC       2
#define ET_REL        1

// p_type values
#define PT_LOAD       1

// p_flags values
#define PF_X          1 // Execute
#define PF_W          2 // Write
#define PF_R          4 // Read

// Section Header
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

// Symbol Table Entry
typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

// Relocation Entry (Addend)
typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

// ELF64_R_SYM and ELF64_R_TYPE macros
#define ELF64_R_SYM(info) ((info) >> 32)
#define ELF64_R_TYPE(info) ((info) & 0xFFFFFFFF)

// Relocation types for x86-64
#define R_X86_64_64       1
#define R_X86_64_PC32     2
#define R_X86_64_32       10
#define R_X86_64_32S      11

typedef struct {
    uint64_t entry_point;
    uint64_t program_break;
} elf_load_result_t;

elf_load_result_t elf_load(pml4_t* pml4, const uint8_t* elf_data);
int elf_exec_as_thread(const char* path, int argc, char* argv[]);

#endif // ELF_H