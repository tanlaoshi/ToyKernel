/*
 * DeviceEnumPciTree.c — PR-DEV-tree-pci：扁平枚举后按 PCI 桥建父子边。
 *
 * 算法（规格 设备管理器设备树.md §3.3）：
 *   A. 对每个 PCI 设备，找"最近桥"——候选桥中 Secondary 最大且
 *      Secondary ≤ Dev.PciBus 者，且 Dev.PciBus ∈ [Sec, Sub]。挂到该桥下。
 *   B. Host 浅挂：Bus0 上仍无 Parent 的设备挂到 Host Bridge（Class 0x06/0x00）。
 *
 * 只读配置空间（PciReadConfig）；不写 Command / BAR。
 * 无桥时为空操作，仍可编。
 */
#include "Device.h"
#include "PCIe.h"
#include "Debug.h"

/* Type-1 头 offset 0x18：byte0=Primary, byte1=Secondary, byte2=Subordinate */
#define PCI_BRIDGE_BUS_REG 0x18

static int gLinkEdges;

static void ReadBridgeBuses(DEVICE_NODE *Br, UINT8 *Sec, UINT8 *Sub) {
    UINT32 V = PciReadConfig(Br->PciBus, Br->PciDev, Br->PciFn, PCI_BRIDGE_BUS_REG);

    *Sec = (UINT8)((V >> 8) & 0xFFu);
    *Sub = (UINT8)((V >> 16) & 0xFFu);
}

/* 桥 = Class 0x06 且非 Host（Subclass 0x00 是 Host，由 B 步处理）。
 * 只有 Type-1 头（HeaderType=1）才有 Sec/Sub；ISA 桥等虽是 0x06/0x01 但
 * Type-0 头，offset 0x18 不是总线号，读了会拿到 BAR 垃圾值。 */
static int IsPciBridge(DEVICE_NODE *Dev) {
    UINT32 Ht;

    if (Dev->Bus != DEVICE_BUS_PCI) {
        return 0;
    }
    if (Dev->Class != 0x06) {
        return 0;
    }
    if (Dev->Subclass == 0x00) {
        return 0;
    }
    Ht = PciReadConfig(Dev->PciBus, Dev->PciDev, Dev->PciFn, 0x0C);
    if (((Ht >> 16) & 0x7Fu) != 0x01) {
        return 0;
    }
    return 1;
}

static int IsHostBridge(DEVICE_NODE *Dev) {
    return (Dev->Bus == DEVICE_BUS_PCI &&
            Dev->Class == 0x06 && Dev->Subclass == 0x00);
}

/* A 步：每个 PCI 设备找最近桥并挂接（桥也参与，形成桥嵌套） */
static void LinkBridges(void) {
    int N = DeviceCount();
    int i;
    int j;

    for (i = 0; i < N; i++) {
        DEVICE_NODE *Dev = DeviceGet(i);
        UINT8 BestSec = 0;
        DEVICE_NODE *Best = 0;

        if (Dev->Bus != DEVICE_BUS_PCI) {
            continue;
        }
        for (j = 0; j < N; j++) {
            DEVICE_NODE *Br = DeviceGet(j);
            UINT8 Sec;
            UINT8 Sub;

            if (!IsPciBridge(Br)) {
                continue;
            }
            if (Br == Dev) {
                continue;
            }
            ReadBridgeBuses(Br, &Sec, &Sub);
            /* 无效范围跳过 */
            if (Sec > Sub) {
                continue;
            }
            /* Dev.PciBus 必须落在 [Sec, Sub] */
            if (Dev->PciBus < Sec || Dev->PciBus > Sub) {
                continue;
            }
            /* 候选：取 Sec 最大且 ≤ PciBus 者 */
            if (Sec <= Dev->PciBus && Sec >= BestSec) {
                BestSec = Sec;
                Best = Br;
            }
        }
        if (Best) {
            DeviceSetParent(Dev, Best);
            gLinkEdges++;
        }
    }
}

/* B 步：Host 浅挂——Bus0 上无 Parent 的设备挂到 Host Bridge */
static void LinkHostShallow(void) {
    int N = DeviceCount();
    int i;
    DEVICE_NODE *Host = 0;

    /* 找 Host：第一个 0x06/0x00；多个时 PciBus==0 优先 */
    for (i = 0; i < N; i++) {
        DEVICE_NODE *Node = DeviceGet(i);

        if (!IsHostBridge(Node)) {
            continue;
        }
        if (Node->PciBus == 0) {
            Host = Node;
            break;
        }
        if (!Host) {
            Host = Node;
        }
    }
    if (!Host) {
        return;
    }
    for (i = 0; i < N; i++) {
        DEVICE_NODE *Dev = DeviceGet(i);

        if (Dev == Host || Dev->Bus != DEVICE_BUS_PCI) {
            continue;
        }
        if (Dev->Parent) {
            continue; /* 已挂在桥下，不再改挂 Host */
        }
        if (Dev->PciBus == 0) {
            DeviceSetParent(Dev, Host);
            gLinkEdges++;
        }
    }
}

void HalDeviceLinkPciTree(void) {
    gLinkEdges = 0;
    LinkBridges();
    LinkHostShallow();
    DebugWrite("DeviceEnumPciTree: linked ");
    DebugHex32((UINT32)gLinkEdges);
    DebugWrite(" edges\n");
}
