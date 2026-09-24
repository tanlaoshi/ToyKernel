/*
 * DeviceResource.c — PR-DEV-mmio-conflict：MMIO 区间登记与重叠检测。
 *
 * 平台无关：只持表 + 半开区间重叠判定；不读配置空间（IO/MMIO 判别由 HAL 做）。
 * 策略 A：HAL 枚举扫完后对表内每台 PCI 设备的 MMIO BAR 调本函数登记。
 * 冲突只 DebugWrite 并返回 -1；不改写配置空间、不重分配 BAR。
 *
 * 约束：新 .c ≤300；Core 不 include HAL 私有头；驱动零改动；只检测 MMIO。
 */
#include "Device.h"
#include "Debug.h"

#define MMIO_REGION_MAX 64

typedef struct {
    UINT64 Base;
    UINT64 Size;
    DEVICE_NODE *Owner;
} RESOURCE_REGION;

static RESOURCE_REGION gMmioRegions[MMIO_REGION_MAX];
static int gMmioCount = 0;

/* 半开区间 [Base, Base+Size) 与 [Other, Other+Other.Size) 重叠：
 * Base < Other+Other.Size && Base+Size > Other。 */
static int Overlaps(UINT64 Base, UINT64 Size, const RESOURCE_REGION *R) {
    if (Size == 0 || R->Size == 0) {
        return 0;
    }
    if (Base < R->Base + R->Size && Base + Size > R->Base) {
        return 1;
    }
    return 0;
}

static void WriteRegion(UINT64 Base, UINT64 Size) {
    DebugWrite("[");
    DebugHex32((UINT32)(Base >> 32));
    DebugHex32((UINT32)Base);
    DebugWrite("+");
    DebugHex32((UINT32)(Size >> 32));
    DebugHex32((UINT32)Size);
    DebugWrite(")");
}

int DeviceRegisterMmio(DEVICE_NODE *Dev, UINT64 Base, UINT64 Size) {
    int i;

    if (Size == 0 || Base == 0) {
        return 0;
    }
    if (gMmioCount >= MMIO_REGION_MAX) {
        DebugWrite("MMIO: region table full\n");
        return -1;
    }
    for (i = 0; i < gMmioCount; i++) {
        if (Overlaps(Base, Size, &gMmioRegions[i])) {
            DebugWrite("MMIO conflict: ");
            if (Dev && Dev->Name[0]) {
                DebugWrite(Dev->Name);
            } else {
                DebugWrite("?");
            }
            DebugWrite(" ");
            WriteRegion(Base, Size);
            DebugWrite(" overlaps ");
            if (gMmioRegions[i].Owner && gMmioRegions[i].Owner->Name[0]) {
                DebugWrite(gMmioRegions[i].Owner->Name);
            } else {
                DebugWrite("?");
            }
            DebugWrite(" ");
            WriteRegion(gMmioRegions[i].Base, gMmioRegions[i].Size);
            DebugWrite("\n");
            return -1;
        }
    }
    gMmioRegions[gMmioCount].Base = Base;
    gMmioRegions[gMmioCount].Size = Size;
    gMmioRegions[gMmioCount].Owner = Dev;
    gMmioCount++;
    return 0;
}
