/*
 * Drv.h — 简单驱动框架公开面（PR-D1）
 * Common 只依赖本头；硬件细节留在 HAL/<Arch>/Drivers。
 */
#ifndef DRV_H
#define DRV_H

#include "BootTypes.h"

#define TOY_DRV_MAX_DRIVERS  16
#define TOY_DRV_MAX_INSTANCES 16

typedef enum {
    TOY_DRV_CLASS_NONE = 0,
    TOY_DRV_CLASS_BLOCK,
    TOY_DRV_CLASS_INPUT,
    TOY_DRV_CLASS_NET,
    TOY_DRV_CLASS_DISPLAY
} TOY_DRV_CLASS;

typedef struct TOY_DRV_INSTANCE TOY_DRV_INSTANCE;

typedef struct TOY_DRIVER {
    const char *Name;
    TOY_DRV_CLASS Class;
    /* 匹配表占位（PCI/virtio/DTB）；D2+ 使用，D1 可为 NULL */
    const void *Match;
    /*
     * Probe：有设备则返回 0 并可选写入 *OutPriv；无设备返回非 0。
     * BusCtx 预留总线上下文（D2+）。
     */
    int (*Probe)(const struct TOY_DRIVER *Self, void *BusCtx, void **OutPriv);
    int (*Bind)(TOY_DRV_INSTANCE *Inst);
    void (*Remove)(TOY_DRV_INSTANCE *Inst);
} TOY_DRIVER;

struct TOY_DRV_INSTANCE {
    const TOY_DRIVER *Drv;
    void *Priv;
    int Bound;
};

int ToyDrvRegister(const TOY_DRIVER *Drv);
/* 对已注册驱动调用 Probe；成功则 Bind 并记入实例表 */
int ToyDrvProbeAll(void);
/* PR-D2：只 Probe 指定类；已有该驱动实例则跳过 */
int ToyDrvProbeClass(TOY_DRV_CLASS Class);
void ToyDrvRemoveAll(void);

UINTN ToyDrvRegisteredCount(void);
const TOY_DRIVER *ToyDrvRegisteredGet(UINTN Index);
UINTN ToyDrvInstanceCount(void);
const TOY_DRV_INSTANCE *ToyDrvInstanceGet(UINTN Index);

#endif
