/*
 * Drv.c — 驱动注册表与 Probe/Bind/Remove 生命周期（PR-D1）
 */
#include "Drv.h"
#include "Hal.h"

static const TOY_DRIVER *gDrivers[TOY_DRV_MAX_DRIVERS];
static UINTN gDriverCount;

static TOY_DRV_INSTANCE gInstances[TOY_DRV_MAX_INSTANCES];
static UINTN gInstanceCount;

int ToyDrvRegister(const TOY_DRIVER *Drv) {
    if (!Drv || !Drv->Name || !Drv->Probe) {
        return -1;
    }
    if (gDriverCount >= TOY_DRV_MAX_DRIVERS) {
        HalDebugWrite("drv: register full\n");
        return -1;
    }
    gDrivers[gDriverCount++] = Drv;
    return 0;
}

int ToyDrvProbeAll(void) {
    return ToyDrvProbeClass(TOY_DRV_CLASS_NONE);
}

static int DriverAlreadyBound(const TOY_DRIVER *D) {
    UINTN i;
    for (i = 0; i < gInstanceCount; i++) {
        if (gInstances[i].Drv == D && gInstances[i].Bound) {
            return 1;
        }
    }
    return 0;
}

/*
 * Class == TOY_DRV_CLASS_NONE：Probe 全部（D1 行为）。
 * 其它：只 Probe 该类；已绑定的驱动跳过（供 HalBlockInit 在 VMM 后再试 virtio-blk）。
 */
int ToyDrvProbeClass(TOY_DRV_CLASS Class) {
    UINTN i;
    int Bound = 0;

    for (i = 0; i < gDriverCount; i++) {
        const TOY_DRIVER *D = gDrivers[i];
        void *Priv = 0;
        TOY_DRV_INSTANCE *Inst;

        if (Class != TOY_DRV_CLASS_NONE && D->Class != Class) {
            continue;
        }
        if (DriverAlreadyBound(D)) {
            continue;
        }
        if (D->Probe(D, 0, &Priv) != 0) {
            continue;
        }
        if (gInstanceCount >= TOY_DRV_MAX_INSTANCES) {
            HalDebugWrite("drv: instance full\n");
            if (D->Remove) {
                TOY_DRV_INSTANCE Tmp;
                Tmp.Drv = D;
                Tmp.Priv = Priv;
                Tmp.Bound = 0;
                D->Remove(&Tmp);
            }
            break;
        }
        Inst = &gInstances[gInstanceCount];
        Inst->Drv = D;
        Inst->Priv = Priv;
        Inst->Bound = 0;
        if (D->Bind) {
            if (D->Bind(Inst) != 0) {
                if (D->Remove) {
                    D->Remove(Inst);
                }
                Inst->Drv = 0;
                Inst->Priv = 0;
                continue;
            }
        }
        Inst->Bound = 1;
        gInstanceCount++;
        Bound++;
    }
    HalDebugWrite("drv: registered=");
    HalDebugHex32((UINT32)gDriverCount);
    HalDebugWrite(" bound=");
    HalDebugHex32((UINT32)gInstanceCount);
    HalDebugWrite(" (+");
    HalDebugHex32((UINT32)Bound);
    HalDebugWrite(")\n");
    return 0;
}

void ToyDrvRemoveAll(void) {
    UINTN i;

    for (i = gInstanceCount; i > 0; i--) {
        TOY_DRV_INSTANCE *Inst = &gInstances[i - 1];
        if (Inst->Drv && Inst->Drv->Remove) {
            Inst->Drv->Remove(Inst);
        }
        Inst->Drv = 0;
        Inst->Priv = 0;
        Inst->Bound = 0;
    }
    gInstanceCount = 0;
}

UINTN ToyDrvRegisteredCount(void) {
    return gDriverCount;
}

const TOY_DRIVER *ToyDrvRegisteredGet(UINTN Index) {
    if (Index >= gDriverCount) {
        return 0;
    }
    return gDrivers[Index];
}

UINTN ToyDrvInstanceCount(void) {
    return gInstanceCount;
}

const TOY_DRV_INSTANCE *ToyDrvInstanceGet(UINTN Index) {
    if (Index >= gInstanceCount) {
        return 0;
    }
    return &gInstances[Index];
}
