#ifndef ELF_H
#define ELF_H

#include "BootTypes.h"
#include "VirtualMemory.h"

#define ELF_MAGIC   0x464C457FULL
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define ET_DYN      3
#define EM_X86_64   62
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PF_X        1
#define PF_W        2
#define PF_R        4

#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_SONAME   14
#define DT_PLTREL   20
#define DT_JMPREL   23

#define STB_GLOBAL  1
#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2

#define R_X86_64_64        1
#define R_X86_64_COPY      5
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8

#define ELF_R_SYM(I)  ((I) >> 32)
#define ELF_R_TYPE(I) ((UINT32)(I))

/* 共享库固定装载基址（主程序约在 0x40000000..0x40004000，栈在 0x40100000） */
#define USER_SO_BASE   0x40080000ULL
#define USER_SO_STRIDE 0x20000ULL
#define ELF_MAX_NEEDED 4
#define ELF_MAX_SO     4

typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} Elf64_Phdr;

typedef struct {
    INT64  d_tag;
    union {
        UINT64 d_val;
        UINT64 d_ptr;
    } d_un;
} Elf64_Dyn;

typedef struct {
    UINT32 st_name;
    UINT8  st_info;
    UINT8  st_other;
    UINT16 st_shndx;
    UINT64 st_value;
    UINT64 st_size;
} Elf64_Sym;

typedef struct {
    UINT64 r_offset;
    UINT64 r_info;
    INT64  r_addend;
} Elf64_Rela;

typedef struct {
    UINT64 Entry;
    UINT64 StackTop;
} ELF_LOAD_RESULT;

typedef struct {
    UINT64       Base;
    const UINT8 *Image;
    UINTN        Size;
    const Elf64_Sym *DynSym;
    const char   *DynStr;
    UINTN        DynSymCount;
} ELF_SO_INFO;

int ElfLoadFromMemory(VM_ADDR_SPACE *Space, const void *Image, UINTN Size,
                      ELF_LOAD_RESULT *Out);

/* 收集主 ELF 的 DT_NEEDED 短名（最多 ELF_MAX_NEEDED）；返回个数，失败 -1 */
int ElfCollectNeeded(const void *Image, UINTN Size, char Names[][16], int Max);

/* 将 ET_DYN 装到 Base；Info 填符号表（指向 Image 内） */
int ElfLoadShared(VM_ADDR_SPACE *Space, const void *Image, UINTN Size,
                  UINT64 Base, ELF_SO_INFO *Info);

/* 解析主 ELF 的 RELA/JMPREL，用已装载 SO 解析 JUMP_SLOT/GLOB_DAT/RELATIVE */
int ElfRelocateProgram(VM_ADDR_SPACE *Space, const void *Image, UINTN Size,
                       const ELF_SO_INFO *Sos, int SoCount);

#endif
