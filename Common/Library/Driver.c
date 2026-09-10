/*
 * Driver.c — 驱动注册表与 Probe/Bind/Remove 生命周期（PR-D1）
 */
#include "Driver.h"
#include "Hal.h"
#include "ToySerialLog.h"

static const TOY_DRIVER *gDrivers[TOY_DRIVER_MAX_DRIVERS];
static UINTN gDriverCount;

static TOY_DRIVER_INSTANCE gInstances[TOY_DRIVER_MAX_INSTANCES];
static UINTN gInstanceCount;

int ToyDriverRegister(const TOY_DRIVER *Driver) {
    if (!Driver || !Driver->Name || !Driver->Probe) {
        return -1;
    }
    if (gDriverCount >= TOY_DRIVER_MAX_DRIVERS) {
        ToyLogDrv("driver: register full\n");
        return -1;
    }
    gDrivers[gDriverCount++] = Driver;
    return 0;
}

int ToyDriverProbeAll(void) {
    return ToyDriverProbeClass(TOY_DRIVER_CLASS_NONE);
}

static int DriverAlreadyBound(const TOY_DRIVER *D) {
    UINTN i;
    for (i = 0; i < gInstanceCount; i++) {
        if (gInstances[i].Driver == D && gInstances[i].Bound) {
            return 1;
        }
    }
    return 0;
}

/*
 * Class == TOY_DRIVER_CLASS_NONE：Probe 全部（D1 行为）。
 * 其它：只 Probe 该类；已绑定的驱动跳过（供 HalBlockInit 在 VMM 后再试 virtio-blk）。
 */
int ToyDriverProbeClass(TOY_DRIVER_CLASS Class) {
    UINTN i;
    int Bound = 0;

    for (i = 0; i < gDriverCount; i++) {
        const TOY_DRIVER *D = gDrivers[i];
        void *Priv = 0;
        TOY_DRIVER_INSTANCE *Inst;

        if (Class != TOY_DRIVER_CLASS_NONE && D->Class != Class) {
            continue;
        }
        if (DriverAlreadyBound(D)) {
            continue;
        }
        if (D->Probe(D, 0, &Priv) != 0) {
            continue;
        }
        if (gInstanceCount >= TOY_DRIVER_MAX_INSTANCES) {
            ToyLogDrv("driver: instance full\n");
            if (D->Remove) {
                TOY_DRIVER_INSTANCE Tmp;
                Tmp.Driver = D;
                Tmp.Priv = Priv;
                Tmp.Bound = 0;
                D->Remove(&Tmp);
            }
            break;
        }
        Inst = &gInstances[gInstanceCount];
        Inst->Driver = D;
        Inst->Priv = Priv;
        Inst->Bound = 0;
        if (D->Bind) {
            if (D->Bind(Inst) != 0) {
                if (D->Remove) {
                    D->Remove(Inst);
                }
                Inst->Driver = 0;
                Inst->Priv = 0;
                continue;
            }
        }
        Inst->Bound = 1;
        gInstanceCount++;
        Bound++;
    }
    ToyLogDrv("driver: registered=");
    ToyLogDrvHex32((UINT32)gDriverCount);
    ToyLogDrv(" bound=");
    ToyLogDrvHex32((UINT32)gInstanceCount);
    ToyLogDrv(" (+");
    ToyLogDrvHex32((UINT32)Bound);
    ToyLogDrv(")\n");
    return 0;
}

void ToyDriverRemoveAll(void) {
    UINTN i;

    for (i = gInstanceCount; i > 0; i--) {
        TOY_DRIVER_INSTANCE *Inst = &gInstances[i - 1];
        if (Inst->Driver && Inst->Driver->Remove) {
            Inst->Driver->Remove(Inst);
        }
        Inst->Driver = 0;
        Inst->Priv = 0;
        Inst->Bound = 0;
    }
    gInstanceCount = 0;
}

UINTN ToyDriverRegisteredCount(void) {
    return gDriverCount;
}

const TOY_DRIVER *ToyDriverRegisteredGet(UINTN Index) {
    if (Index >= gDriverCount) {
        return 0;
    }
    return gDrivers[Index];
}

UINTN ToyDriverInstanceCount(void) {
    return gInstanceCount;
}

const TOY_DRIVER_INSTANCE *ToyDriverInstanceGet(UINTN Index) {
    if (Index >= gInstanceCount) {
        return 0;
    }
    return &gInstances[Index];
}
