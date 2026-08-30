#ifndef VMM_H
#define VMM_H

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

int VmmInit(void);
void VmmEnable(void);
UINT64 VmmKernelCr3(void);
void VmmLoadCr3(UINT64 Cr3);

int VmmMapPage(UINT64 Virt, UINT64 Phys, UINT64 Flags);
int VmmMapRange(UINT64 Virt, UINT64 Phys, UINTN Bytes, UINT64 Flags);

VM_ADDR_SPACE *VmmSpaceCreate(void);
void VmmSpaceDestroy(VM_ADDR_SPACE *Space);
UINT64 VmmSpaceCr3(const VM_ADDR_SPACE *Space);
int VmmSpaceMapPage(VM_ADDR_SPACE *Space, UINT64 Virt, UINT64 Phys, UINT64 Flags);
void *VmmSpaceAllocAndTrack(VM_ADDR_SPACE *Space);

int VmmUserAccessOk(UINT64 Virt, UINTN Len);
int CopyFromUser(void *Dst, UINT64 UserSrc, UINTN Len);

#endif
