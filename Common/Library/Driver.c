/*
 * Driver.c — 驱动注册表与 Probe/Bind/Remove 生命周期（PR-D1）
 */
#include "Driver.h"
#include "Device.h"
#include "Hal.h"
#include "ToySerialLog.h"
#include "Debug.h"

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
 *
 * PR-DRV-match-logic：
 *   Match==NULL → 旧路径 Probe(Self, 0, …)（驱动自扫 PCI/DTB）。
 *   Match!=NULL → 遍历未绑定设备，DriverMatchesDevice 命中则
 *                 Probe(Self, (void *)Dev, …)，成功必须 DeviceBindDriver。
 *                 一驱动一实例：命中即 break。
 */
static TOY_DRIVER_INSTANCE *DriverAdoptInstance(const TOY_DRIVER *D, void *Private) {
    TOY_DRIVER_INSTANCE *Inst;

    if (gInstanceCount >= TOY_DRIVER_MAX_INSTANCES) {
        ToyLogDrv("driver: instance full\n");
        if (D->Remove) {
            TOY_DRIVER_INSTANCE Tmp;
            Tmp.Driver = D;
            Tmp.Private = Private;
            Tmp.Bound = 0;
            D->Remove(&Tmp);
        }
        return 0;
    }
    Inst = &gInstances[gInstanceCount];
    Inst->Driver = D;
    Inst->Private = Private;
    Inst->Bound = 0;
    if (D->Bind && D->Bind(Inst) != 0) {
        if (D->Remove) {
            D->Remove(Inst);
        }
        Inst->Driver = 0;
        Inst->Private = 0;
        return 0;
    }
    Inst->Bound = 1;
    gInstanceCount++;
    return Inst;
}

int ToyDriverProbeClass(TOY_DRIVER_CLASS Class) {
    UINTN i;
    int Bound = 0;

    for (i = 0; i < gDriverCount; i++) {
        const TOY_DRIVER *D = gDrivers[i];
        void *Private = 0;
        TOY_DRIVER_INSTANCE *Inst;

        if (Class != TOY_DRIVER_CLASS_NONE && D->Class != Class) {
            continue;
        }
        if (DriverAlreadyBound(D)) {
            continue;
        }
        if (D->Match == NULL) {
            if (D->Probe(D, 0, &Private) != 0) {
                continue;
            }
            if (DriverAdoptInstance(D, Private)) {
                Bound++;
            }
        } else {
            int Idx;
            for (Idx = 0; Idx < DeviceCount(); Idx++) {
                DEVICE_NODE *Dev = DeviceGet(Idx);
                if (!Dev || Dev->Bound) {
                    continue;
                }
                if (!DriverMatchesDevice(D, Dev)) {
                    continue;
                }
                if (D->Probe(D, (void *)Dev, &Private) != 0) {
                    continue;
                }
                Inst = DriverAdoptInstance(D, Private);
                if (Inst) {
                    DeviceBindDriver(Dev, D, Inst);
                    Bound++;
                }
                break; /* 一驱动一实例 */
            }
        }
    }
#if TOY_KERNEL_DEBUG
    ToyLogDrv("driver: registered=");
    ToyLogDrvHex32((UINT32)gDriverCount);
    ToyLogDrv(" bound=");
    ToyLogDrvHex32((UINT32)gInstanceCount);
    ToyLogDrv(" (+");
    ToyLogDrvHex32((UINT32)Bound);
    ToyLogDrv(")\n");
#else
    (void)Bound;
#endif
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
        Inst->Private = 0;
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
