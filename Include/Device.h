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

/* 绑定观察态（PR-DEV-node；策略 A 填充归 enum 刀） */
typedef enum {
    DEVICE_STATE_UNBOUND = 0,
    DEVICE_STATE_BOUND,
    DEVICE_STATE_DISABLED, /* 预留 */
    DEVICE_STATE_ERROR     /* 预留 */
} DEVICE_STATE;

/* 设备节点 */
typedef struct DEVICE_NODE {
    char Name[32];
    char FriendlyName[64]; /* 人类标题；空则回退 Name */
    DEVICE_BUS Bus;
    DEVICE_STATE State;

    /* 匹配信息 */
    UINT16 Vendor;
    UINT16 Device;
    char Compatible[64];

    /* PCI 类码（非 PCI 填 0；PR-DEV-node） */
    UINT8 Class;
    UINT8 Subclass;
    UINT8 ProgIf;

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
/* PR-DEV-tree-api：父子拓扑 API（策略 A：只存 Parent，扫表取 Children）。 */
/* 设 Child->Parent = Parent。两指针必须落在 gDevices[] 表内（NULL 表示摘下，允许）。
 * 沿 Parent 链做环检测：若 Parent 链上已出现 Child 则拒绝返回 -1。成功返回 0。 */
int DeviceSetParent(DEVICE_NODE *Child, DEVICE_NODE *Parent);
/* 返回 Dev->Parent（Dev 为 NULL 时返回 NULL）。 */
DEVICE_NODE *DeviceGetParent(DEVICE_NODE *Dev);
/* 扫 gDevices[]：Parent==Me 的填入 Out[0..Max)；返回命中个数。Max 满则截断。 */
int DeviceGetChildren(DEVICE_NODE *Parent, DEVICE_NODE **Out, int Max);
/* PR-DEV-tree-api：DEBUG 自检（release 下空实现）。枚举后由 DeviceEnumerateAll 调用。 */
void DeviceTreeSelfTest(void);
void DeviceBindDriver(DEVICE_NODE *Dev, const struct TOY_DRIVER *Drv,
                      struct TOY_DRIVER_INSTANCE *Inst);
void DeviceUnbind(DEVICE_NODE *Dev);
void DeviceListDump(void);
/* PR-DEV-lsdev：解析 -v / -b / -u / -c。0=已打印，-1=用法错误 */
int DeviceListDumpArgs(int Argc, char **Argv);

/*
 * 由启动路径调用（PR-DEV-3）；内部调 HalDeviceEnumerate。
 * 本函数不依赖具体硬件。
 */
void DeviceEnumerateAll(void);
/*
 * PR-DEV-enum 策略 A：已绑定 ToyDriverInstance 按名认领一台 PCI 设备，
 * 回填 State / Bound / Driver。实例表无 BDF，同名多台只认第一台。
 */
void DeviceSyncBound(void);

#endif
