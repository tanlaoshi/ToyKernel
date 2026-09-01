/*
 * VirtualMemory.h — 虚拟内存策略层接口
 */
#ifndef VIRTUAL_MEMORY_H
#define VIRTUAL_MEMORY_H

#include "BootConfig.h"
#include "hal.h"

#define PTE_PRESENT  HAL_PAGE_PRESENT
#define PTE_WRITABLE HAL_PAGE_WRITABLE
#define PTE_USER     HAL_PAGE_USER

#define USER_CODE_VIRT  0x40000000ULL
#define USER_STACK_VIRT 0x40100000ULL
#define USER_STACK_SIZE 0x4000ULL
#define USER_VIRT_END   (USER_STACK_VIRT + USER_STACK_SIZE)

#define VM_SPACE_MAX_PAGES 128

typedef struct {
    UINT64 Root;
    void   *Pages[VM_SPACE_MAX_PAGES];
    int     PageCount;
} VM_ADDR_SPACE;

int VirtualMemoryInit(void);
void VirtualMemoryEnable(void);
UINT64 VirtualMemoryKernelCr3(void);
void VirtualMemoryLoadCr3(UINT64 Cr3);

int VirtualMemoryMapPage(UINT64 Virt, UINT64 Phys, UINT64 Flags);
int VirtualMemoryMapRange(UINT64 Virt, UINT64 Phys, UINTN Bytes, UINT64 Flags);

VM_ADDR_SPACE *VirtualMemorySpaceCreate(void);
void VirtualMemorySpaceDestroy(VM_ADDR_SPACE *Space);
UINT64 VirtualMemorySpaceCr3(const VM_ADDR_SPACE *Space);
int VirtualMemorySpaceMapPage(VM_ADDR_SPACE *Space, UINT64 Virt, UINT64 Phys, UINT64 Flags);
void *VirtualMemorySpaceAllocateAndTrack(VM_ADDR_SPACE *Space);

int VirtualMemoryUserAccessOk(UINT64 Virt, UINTN Len);
int VirtualMemoryCopyFromUser(void *Dst, UINT64 UserSrc, UINTN Len);
int VirtualMemoryCopyToUser(UINT64 UserDst, const void *Src, UINTN Len);

VM_ADDR_SPACE *VirtualMemorySpaceClone(VM_ADDR_SPACE *Src);

#endif
