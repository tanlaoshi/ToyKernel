/*
 * Driver.h — 简单驱动框架公开面（PR-D1）
 * Common 只依赖本头；硬件细节留在 HAL/<Arch>/Drivers。
 */
#ifndef DRIVER_H
#define DRIVER_H

#include "BootTypes.h"

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

typedef struct TOY_DRIVER {
    const char *Name;
    TOY_DRIVER_CLASS Class;
    /* 匹配表占位（PCI/virtio/DTB）；D2+ 使用，D1 可为 NULL */
    const void *Match;
    /*
     * Probe：有设备则返回 0 并可选写入 *OutPriv；无设备返回非 0。
     * BusCtx 预留总线上下文（D2+）。
     */
    int (*Probe)(const struct TOY_DRIVER *Self, void *BusCtx, void **OutPriv);
    int (*Bind)(TOY_DRIVER_INSTANCE *Inst);
    void (*Remove)(TOY_DRIVER_INSTANCE *Inst);
} TOY_DRIVER;

struct TOY_DRIVER_INSTANCE {
    const TOY_DRIVER *Driver;
    void *Priv;
    int Bound;
};

int ToyDriverRegister(const TOY_DRIVER *Driver);
/* 对已注册驱动调用 Probe；成功则 Bind 并记入实例表 */
int ToyDriverProbeAll(void);
/* PR-D2：只 Probe 指定类；已有该驱动实例则跳过 */
int ToyDriverProbeClass(TOY_DRIVER_CLASS Class);
void ToyDriverRemoveAll(void);

UINTN ToyDriverRegisteredCount(void);
const TOY_DRIVER *ToyDriverRegisteredGet(UINTN Index);
UINTN ToyDriverInstanceCount(void);
const TOY_DRIVER_INSTANCE *ToyDriverInstanceGet(UINTN Index);

#endif
