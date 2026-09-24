/*
 * PhysicalMemory.c — 物理页分配器入口。位图在段表里。
 */
#include "PhysicalMemory.h"
#include "PhysicalMemoryPrivate.h"
#include "BootInfo.h"
#include "Debug.h"

void PmmPhysLockInit(void);

int PhysicalMemoryInitialize(void)
{
    const BOOT_INFO *Info = BootInfoGet();

    if (Info == 0 || Info->RegionCount == 0) {
        return -1;
    }
    PmmPhysLockInit();
    PmmSegmentInit(Info);
    DebugWrite("PMM: free=");
    DebugHex64(PmmTrackedFreePages() << PAGE_SHIFT);
    DebugWrite(" / tracked=");
    DebugHex64(PhysicalMemoryTotalPages() << PAGE_SHIFT);
    DebugWrite("\n");
    return 0;
}
