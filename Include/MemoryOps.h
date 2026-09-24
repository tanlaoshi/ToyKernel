/*
 * MemoryOps.h — 可替换物理页分配政策。框架持 gPhysLock 后调用 *Locked。
 */
#ifndef MEMORY_OPS_H
#define MEMORY_OPS_H

#include "BootTypes.h"

typedef struct {
    /*
     * Init：政策侧初始化
     *   - 默认 Bitmap 政策：空实现
     *   - 学生政策：初始化自己的数据结构
     *   - 调用时机：PmmSegmentInit 之后
     */
    void (*Init)(void);

    /*
     * AllocPagesLocked(Count)
     *   - Count == 0 → 返回 NULL
     *   - 无空闲连续页 → 返回 NULL
     *   - 成功 → 返回物理地址（恒等映射下的虚拟地址）
     *   - 调用约定：框架已持 gPhysLock；实现不应再加锁
     */
    void *(*AllocPagesLocked)(UINT32 Count);

    /*
     * FreePagesLocked(Page, Count)
     *   - Page == NULL 或 Count == 0 → 空操作
     *   - Page 不在任何段 → DebugWrite 警告，空操作
     *   - 调用约定：框架已持 gPhysLock
     */
    void (*FreePagesLocked)(void *Page, UINT32 Count);

    /*
     * RetainPageLocked(Page)
     *   - Page == NULL → 返回 -1
     *   - Page 不在任何段 → 返回 -1
     *   - Page 未分配（RefCount == 0） → 返回 -1
     *   - RefCount 饱和（0xFFFF） → 返回 -1
     *   - 成功 → 返回 0
     *   - 调用约定：框架已持 gPhysLock
     */
    int (*RetainPageLocked)(void *Page);

    /*
     * ReleasePageLocked(Page)
     *   - Page == NULL → 空操作
     *   - Page 不在任何段 → DebugWrite 警告，空操作
     *   - Page 未分配 → DebugWrite 警告，空操作
     *   - RefCount 减到 0 → 清位图位 + FreePages++
     *   - 调用约定：框架已持 gPhysLock
     */
    void (*ReleasePageLocked)(void *Page);
} MEMORY_OPS;

void MemoryOpsRegister(const MEMORY_OPS *Ops);
const MEMORY_OPS *MemoryOpsGet(void);
const MEMORY_OPS *MemoryBitmapOps(void);
const MEMORY_OPS *MemoryBestFitOps(void);

#endif
