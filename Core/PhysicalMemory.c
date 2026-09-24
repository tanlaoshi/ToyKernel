/*
 * PhysicalMemory.c — 物理页分配器入口。位图在段表里。
 */
#include "PhysicalMemory.h"
#include "PhysicalMemoryPrivate.h"
#include "MemoryOps.h"
#include "BootInfo.h"
#include "Debug.h"

void PmmPhysLockInit(void);

int PhysicalMemoryInitialize(void)
{
    const BOOT_INFO *Info = BootInfoGet();
    const MEMORY_OPS *Ops;

    if (Info == 0 || Info->RegionCount == 0) {
        return -1;
    }
    PmmPhysLockInit();
    PmmSegmentInit(Info);
    MemoryOpsRegister(MemoryBitmapOps());
    Ops = MemoryOpsGet();
    if (Ops && Ops->Init) {
        Ops->Init();
    }
    DebugWrite("PMM: free=");
    DebugHex64(PhysicalMemoryFreePageCount() << PAGE_SHIFT);
    DebugWrite(" / tracked=");
    DebugHex64(PhysicalMemoryTotalPages() << PAGE_SHIFT);
    DebugWrite("\n");
    return 0;
}
