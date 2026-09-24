/*
 * Device.c — 平台无关设备表（PR-DEV-1）
 *
 * 不 include Hal.h；HalDeviceEnumerate 仅前向声明，定义在各 Arch DeviceEnum.c。
 */
#include "Device.h"
#include "Driver.h"
#include "PciNames.h"

#define DEVICE_MAX 128

static DEVICE_NODE gDevices[DEVICE_MAX];
static int gDeviceCount;

/* 前向：各 Arch DeviceEnum.c；本文件禁止再出现其它 Hal* */
void HalDeviceEnumerate(void);

static void ZeroNode(DEVICE_NODE *N) {
    UINT8 *P;
    UINTN i;
    UINTN Bytes;

    if (!N) {
        return;
    }
    P = (UINT8 *)N;
    Bytes = (UINTN)sizeof(DEVICE_NODE);
    for (i = 0; i < Bytes; i++) {
        P[i] = 0;
    }
}

static void CopyStr(char *Dst, UINTN Max, const char *Src) {
    UINTN i;

    if (!Dst || Max == 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    for (i = 0; i + 1 < Max && Src[i]; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

static int StrEq(const char *A, const char *B) {
    if (!A || !B) {
        return 0;
    }
    while (*A && *A == *B) {
        A++;
        B++;
    }
    return *A == *B;
}

void DeviceInitialize(void) {
    int i;

    for (i = 0; i < DEVICE_MAX; i++) {
        ZeroNode(&gDevices[i]);
    }
    gDeviceCount = 0;
}

int DeviceAdd(const DEVICE_NODE *Dev) {
    DEVICE_NODE *Slot;
    int b;

    if (!Dev) {
        return -1;
    }
    if (gDeviceCount >= DEVICE_MAX) {
        return -1;
    }
    Slot = &gDevices[gDeviceCount];
    ZeroNode(Slot);
    CopyStr(Slot->Name, sizeof(Slot->Name), Dev->Name);
    CopyStr(Slot->FriendlyName, sizeof(Slot->FriendlyName), Dev->FriendlyName);
    Slot->Bus = Dev->Bus;
    Slot->State = Dev->State;
    Slot->Vendor = Dev->Vendor;
    Slot->Device = Dev->Device;
    CopyStr(Slot->Compatible, sizeof(Slot->Compatible), Dev->Compatible);
    Slot->Class = Dev->Class;
    Slot->Subclass = Dev->Subclass;
    Slot->ProgIf = Dev->ProgIf;
    Slot->PciBus = Dev->PciBus;
    Slot->PciDev = Dev->PciDev;
    Slot->PciFn = Dev->PciFn;
    for (b = 0; b < 6; b++) {
        Slot->Bar[b] = Dev->Bar[b];
    }
    Slot->Irq = Dev->Irq;
    Slot->Parent = Dev->Parent;
    Slot->Driver = Dev->Driver;
    Slot->Instance = Dev->Instance;
    Slot->Bound = Dev->Bound ? 1 : 0;
    /* PCI 且调用方未填友好名：用课用表回填 */
    if (Slot->Bus == DEVICE_BUS_PCI && Slot->FriendlyName[0] == 0) {
        const char *Fn = PciGetDeviceName(Slot->Vendor, Slot->Device);

        if (Fn) {
            CopyStr(Slot->FriendlyName, sizeof(Slot->FriendlyName), Fn);
        }
    }
    gDeviceCount++;
    return gDeviceCount - 1;
}

int DeviceCount(void) {
    return gDeviceCount;
}

DEVICE_NODE *DeviceGet(int Idx) {
    if (Idx < 0 || Idx >= gDeviceCount) {
        return 0;
    }
    return &gDevices[Idx];
}

DEVICE_NODE *DeviceFindByName(const char *Name) {
    int i;

    if (!Name) {
        return 0;
    }
    for (i = 0; i < gDeviceCount; i++) {
        if (StrEq(gDevices[i].Name, Name)) {
            return &gDevices[i];
        }
    }
    return 0;
}

DEVICE_NODE *DeviceFindByPci(UINT16 Vendor, UINT16 Device) {
    int i;

    for (i = 0; i < gDeviceCount; i++) {
        if (gDevices[i].Vendor == Vendor && gDevices[i].Device == Device) {
            return &gDevices[i];
        }
    }
    return 0;
}

DEVICE_NODE *DeviceFindByCompatible(const char *Compatible) {
    int i;

    if (!Compatible) {
        return 0;
    }
    for (i = 0; i < gDeviceCount; i++) {
        if (StrEq(gDevices[i].Compatible, Compatible)) {
            return &gDevices[i];
        }
    }
    return 0;
}

void DeviceBindDriver(DEVICE_NODE *Dev, const struct TOY_DRIVER *Drv,
                      struct TOY_DRIVER_INSTANCE *Inst) {
    if (!Dev) {
        return;
    }
    Dev->Driver = Drv;
    Dev->Instance = Inst;
    Dev->Bound = 1;
}

void DeviceUnbind(DEVICE_NODE *Dev) {
    if (!Dev) {
        return;
    }
    Dev->Driver = 0;
    Dev->Instance = 0;
    Dev->Bound = 0;
}

void DeviceEnumerateAll(void) {
    HalDeviceEnumerate();
    DeviceTreeSelfTest();
}

static int StartsWith(const char *S, const char *Prefix) {
    if (!S || !Prefix) {
        return 0;
    }
    while (*Prefix) {
        if (*S != *Prefix) {
            return 0;
        }
        S++;
        Prefix++;
    }
    return 1;
}

/* 驱动名 ↔ 枚举短键。实例没有 PCI 位，一台驱动只认第一台未绑定设备。 */
static int InstanceClaims(const TOY_DRIVER *Drv, const DEVICE_NODE *Node) {
    const char *Name;

    if (!Drv || !Drv->Name || !Node || Node->Bus != DEVICE_BUS_PCI) {
        return 0;
    }
    Name = Drv->Name;
    if (StrEq(Name, Node->Name)) {
        return 1;
    }
    if (StrEq(Node->Name, "xhci") && StartsWith(Name, "xhci")) {
        return 1;
    }
    if (StrEq(Node->Name, "net") &&
        (StartsWith(Name, "e1000") || StartsWith(Name, "virtio-net"))) {
        return 1;
    }
    if (StrEq(Node->Name, "storage") &&
        (StrEq(Name, "virtio-blk") || StrEq(Name, "ata-pio"))) {
        return 1;
    }
    return 0;
}

void DeviceSyncBound(void) {
    int i;
    UINTN k;

    for (i = 0; i < gDeviceCount; i++) {
        if (gDevices[i].Bus != DEVICE_BUS_PCI) {
            continue;
        }
        gDevices[i].State = DEVICE_STATE_UNBOUND;
        gDevices[i].Bound = 0;
        gDevices[i].Driver = 0;
        gDevices[i].Instance = 0;
    }
    for (k = 0; k < ToyDriverInstanceCount(); k++) {
        const TOY_DRIVER_INSTANCE *Inst = ToyDriverInstanceGet(k);

        if (!Inst || !Inst->Bound || !Inst->Driver) {
            continue;
        }
        for (i = 0; i < gDeviceCount; i++) {
            DEVICE_NODE *Node = &gDevices[i];

            if (Node->State == DEVICE_STATE_BOUND) {
                continue;
            }
            if (!InstanceClaims(Inst->Driver, Node)) {
                continue;
            }
            Node->State = DEVICE_STATE_BOUND;
            Node->Bound = 1;
            Node->Driver = Inst->Driver;
            Node->Instance = (struct TOY_DRIVER_INSTANCE *)Inst;
            break;
        }
    }
}
