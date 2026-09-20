/*
 * Device.h — 设备管理器（平台无关）
 *
 * 只管理设备列表；设备来源由 HAL/<Arch>/DeviceEnum.c 提供。
 * Core 不 include 任何 HAL 私有头。
 */
#ifndef DEVICE_H
#define DEVICE_H

#include "BootTypes.h"

/* 前向声明（避免 include Driver.h） */
struct TOY_DRIVER;
struct TOY_DRIVER_INSTANCE;

/* 设备总线类型 */
typedef enum {
    DEVICE_BUS_NONE = 0,
    DEVICE_BUS_PCI,
    DEVICE_BUS_ACPI,
    DEVICE_BUS_DTB,
    DEVICE_BUS_MMIO,
    DEVICE_BUS_FIXED
} DEVICE_BUS;

/* 设备节点 */
typedef struct DEVICE_NODE {
    char Name[32];
    DEVICE_BUS Bus;

    /* 匹配信息 */
    UINT16 Vendor;
    UINT16 Device;
    char Compatible[64];

    /* PCI 位置（非 PCI 填 0；供 lsdev [bus:dev.fn]） */
    UINT8 PciBus;
    UINT8 PciDev;
    UINT8 PciFn;

    /* 资源 */
    UINT64 Bar[6];
    UINT8 Irq;

    /* 父设备（指向表内其它节点，可为 NULL） */
    struct DEVICE_NODE *Parent;

    /* 驱动绑定 */
    const struct TOY_DRIVER *Driver;
    struct TOY_DRIVER_INSTANCE *Instance;
    int Bound;
} DEVICE_NODE;

/* ---- 设备管理器 API ---- */

void DeviceInitialize(void);
/* 复制到表；成功返回索引 ≥0，满/空参返回 -1 */
int DeviceAdd(const DEVICE_NODE *Dev);
int DeviceCount(void);
DEVICE_NODE *DeviceGet(int Idx);
DEVICE_NODE *DeviceFindByName(const char *Name);
DEVICE_NODE *DeviceFindByPci(UINT16 Vendor, UINT16 Device);
DEVICE_NODE *DeviceFindByCompatible(const char *Compatible);
void DeviceBindDriver(DEVICE_NODE *Dev, const struct TOY_DRIVER *Drv,
                      struct TOY_DRIVER_INSTANCE *Inst);
void DeviceUnbind(DEVICE_NODE *Dev);
void DeviceListDump(void);

/*
 * 由启动路径调用（PR-DEV-3）；内部调 HalDeviceEnumerate。
 * 本函数不依赖具体硬件。
 */
void DeviceEnumerateAll(void);

#endif
