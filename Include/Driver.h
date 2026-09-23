/*
 * Driver.h — 简单驱动框架公开面（PR-D1）
 * Common 只依赖本头；硬件细节留在 HAL/<Arch>/Drivers。
 */
#ifndef DRIVER_H
#define DRIVER_H

#include "BootTypes.h"
/* DEVICE_NODE 用于 DriverMatchesDevice 声明；Device.h 不回 include 本头，无环。 */
#include "Device.h"

#define TOY_DRIVER_MAX_DRIVERS  16
#define TOY_DRIVER_MAX_INSTANCES 16

typedef enum {
    TOY_DRIVER_CLASS_NONE = 0,
    TOY_DRIVER_CLASS_BLOCK,
    TOY_DRIVER_CLASS_INPUT,
    TOY_DRIVER_CLASS_NET,
    TOY_DRIVER_CLASS_DISPLAY
} TOY_DRIVER_CLASS;

typedef struct TOY_DRIVER_INSTANCE TOY_DRIVER_INSTANCE;

/*
 * 驱动匹配表（阶段 2）。NULL = 旧行为（Probe 自扫 PCI/DTB）。
 * 表以 DRIVER_MATCH_NONE 结尾；框架按 Type 过滤后把 DEVICE_NODE * 经 BusCtx 传入 Probe。
 */
typedef enum {
    DRIVER_MATCH_NONE = 0,
    DRIVER_MATCH_PCI,
    DRIVER_MATCH_DTB,
    DRIVER_MATCH_FIXED
} DRIVER_MATCH_TYPE;

typedef struct DRIVER_MATCH {
    DRIVER_MATCH_TYPE Type;
    union {
        struct {
            UINT16 Vendor;
            UINT16 Device;
            UINT16 VendorMask;
            UINT16 DeviceMask;
        } Pci;
        struct {
            char Compatible[64];
        } Dtb;
        struct {
            UINT64 Base;
        } Fixed;
    } U;
} DRIVER_MATCH;

typedef struct TOY_DRIVER {
    const char *Name;
    TOY_DRIVER_CLASS Class;
    /* 匹配表（阶段 2 定型）；NULL = 旧行为，Probe 自扫 */
    const DRIVER_MATCH *Match;
    /*
     * Probe：有设备则返回 0 并可选写入 *OutPrivate；无设备返回非 0。
     * BusCtx 预留总线上下文：Match!=NULL 时框架传入 DEVICE_NODE *。
     */
    int (*Probe)(const struct TOY_DRIVER *Self, void *BusCtx, void **OutPrivate);
    int (*Bind)(TOY_DRIVER_INSTANCE *Inst);
    void (*Remove)(TOY_DRIVER_INSTANCE *Inst);
} TOY_DRIVER;

struct TOY_DRIVER_INSTANCE {
    const TOY_DRIVER *Driver;
    void *Private;
    int Bound;
};

int ToyDriverRegister(const TOY_DRIVER *Driver);
/* 对已注册驱动调用 Probe；成功则 Bind 并记入实例表 */
int ToyDriverProbeAll(void);
/* PR-D2：只 Probe 指定类；已有该驱动实例则跳过 */
int ToyDriverProbeClass(TOY_DRIVER_CLASS Class);
void ToyDriverRemoveAll(void);

/* PR-DRV-match-logic：有表驱动按 DRIVER_MATCH 过滤设备；命中返回 1。 */
int DriverMatchesDevice(const TOY_DRIVER *Drv, const DEVICE_NODE *Dev);

UINTN ToyDriverRegisteredCount(void);
const TOY_DRIVER *ToyDriverRegisteredGet(UINTN Index);
UINTN ToyDriverInstanceCount(void);
const TOY_DRIVER_INSTANCE *ToyDriverInstanceGet(UINTN Index);

#endif
