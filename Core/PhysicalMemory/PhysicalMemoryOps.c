/*
 * PhysicalMemoryOps.c — 注册当前物理页分配政策。
 */
#include "MemoryOps.h"

static const MEMORY_OPS *gOps;

void MemoryOpsRegister(const MEMORY_OPS *Ops)
{
    gOps = Ops;
}

const MEMORY_OPS *MemoryOpsGet(void)
{
    return gOps;
}
