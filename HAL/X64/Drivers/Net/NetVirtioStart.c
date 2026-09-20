/*
 * NetVirtioStart.c — PCI 能力与启动（PR-S-virtionet-1）
 */
#include "NetPrivate.h"
#include "PCIe.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"

static int VirtioParseCaps(UINT8 Bus, UINT8 Dev, UINT8 Fn,
                           volatile VIRTIO_COMMON_CFG **Common,
                           volatile VIRTIO_NET_CFG **DevCfg,
                           volatile UINT8 **NotifyBase,
                           UINT32 *NotifyMult) {
    UINT8 CapPtr;
    int Found = 0;

    CapPtr = (UINT8)(PciReadConfig(Bus, Dev, Fn, 0x34) & 0xFF);
    if (CapPtr < 0x40) {
        return 0;
    }

    while (CapPtr >= 0x40) {
        UINT32 Hdr = PciReadConfig(Bus, Dev, Fn, CapPtr);
        UINT8 CapId = (UINT8)(Hdr & 0xFF);
        UINT8 CapNext = (UINT8)((Hdr >> 8) & 0xFF);
        UINT8 CapLen = (UINT8)((Hdr >> 16) & 0xFF);
        if (CapId == PCI_CAP_VENDOR && CapLen >= 16) {
            UINT8 CfgType = (UINT8)((Hdr >> 24) & 0xFF);
            UINT32 Body = PciReadConfig(Bus, Dev, Fn, (UINT8)(CapPtr + 4));
            UINT8 BarIdx = (UINT8)(Body & 0xFF);
            UINT32 OffLo = PciReadConfig(Bus, Dev, Fn, (UINT8)(CapPtr + 8));
            UINT32 Off = OffLo & 0xFFFFFFFC;
            UINT32 BarLo = PciReadConfig(Bus, Dev, Fn, (UINT8)(0x10 + BarIdx * 4));
            UINT64 Base = BarLo & 0xFFFFFFF0ULL;
            volatile UINT8 *Mmio;

            if ((BarLo & 0x6) == 0x4 && BarIdx + 1 < 6) {
                UINT32 BarHi = PciReadConfig(Bus, Dev, Fn, (UINT8)(0x10 + (BarIdx + 1) * 4));
                Base |= (UINT64)BarHi << 32;
            }

            if ((BarLo & 1) == 0 && Base != 0) {
                VirtualMemoryMapRange(Base, Base, 0x100000, PTE_PRESENT | PTE_WRITABLE);
            }
            Mmio = (volatile UINT8 *)(UINTN)(Base + Off);

            if (CfgType == VIRTIO_PCI_CAP_COMMON) {
                *Common = (volatile VIRTIO_COMMON_CFG *)Mmio;
                Found |= 1;
            } else if (CfgType == VIRTIO_PCI_CAP_DEVICE) {
                *DevCfg = (volatile VIRTIO_NET_CFG *)Mmio;
                Found |= 2;
            } else if (CfgType == VIRTIO_PCI_CAP_ISR) {
                gIsr = Mmio;
            } else if (CfgType == VIRTIO_PCI_CAP_NOTIFY) {
                UINT32 Mult = PciReadConfig(Bus, Dev, Fn, (UINT8)(CapPtr + 16));
                *NotifyBase = Mmio;
                *NotifyMult = Mult == 0 ? 1 : Mult;
                Found |= 4;
            }
        }
        CapPtr = CapNext;
    }
    return (Found & 7) == 7;
}

static void VirtioSetFeature(UINT64 Feature) {
    UINT32 DevLo;
    UINT32 DevHi;

    gCommon->DeviceFeatureSelect = 0;
    DevLo = gCommon->DeviceFeature;
    gCommon->DeviceFeatureSelect = 1;
    DevHi = gCommon->DeviceFeature;
    Feature &= ((UINT64)DevHi << 32) | DevLo;
    gCommon->DriverFeatureSelect = 0;
    gCommon->DriverFeature = (UINT32)Feature;
    gCommon->DriverFeatureSelect = 1;
    gCommon->DriverFeature = (UINT32)(Feature >> 32);
}

int VirtioNetStart(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 BarPhys) {
    volatile VIRTIO_COMMON_CFG *Common = 0;
    volatile VIRTIO_NET_CFG *DevCfg = 0;
    volatile UINT8 *NotifyBase = 0;
    UINT32 NotifyMult = 1;
    UINT32 Cmd;
    UINT32 Bar4Lo;
    UINT32 Bar4Hi;
    UINT64 ModernBar;

    Cmd = PciReadConfig(Bus, Dev, Fn, 0x04);
    PciWriteConfig(Bus, Dev, Fn, 0x04, Cmd | 0x07);

    /* Modern virtio-net MMIO is usually BAR4 (64-bit), often above 4GB. */
    Bar4Lo = PciReadConfig(Bus, Dev, Fn, 0x20);
    Bar4Hi = PciReadConfig(Bus, Dev, Fn, 0x24);
    ModernBar = (Bar4Lo & 0xFFFFFFF0ULL) | ((UINT64)Bar4Hi << 32);
    if ((Bar4Lo & 1) == 0 && ModernBar != 0) {
        VirtualMemoryMapRange(ModernBar, ModernBar, 0x10000,
                              PTE_PRESENT | PTE_WRITABLE);
    }
    {
        UINT32 Bar0 = PciReadConfig(Bus, Dev, Fn, 0x10);
        if (Bar0 & 1) {
            gIoPort = (UINT16)(Bar0 & ~3);
        }
    }

    (void)BarPhys;
    if (!VirtioParseCaps(Bus, Dev, Fn, &Common, &DevCfg, &NotifyBase, &NotifyMult)) {
        DebugWrite("Net: missing virtio pci caps\n");
        return -1;
    }
    gCommon = Common;
    gDevCfg = DevCfg;

    Common->DeviceStatus = 0;
    __asm__ volatile("mfence" ::: "memory");
    Common->DeviceStatus = VIRTIO_STATUS_ACK;
    Common->DeviceStatus = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER;
    VirtioSetFeature(VIRTIO_NET_F_MAC | VIRTIO_F_VERSION_1);
    Common->DeviceStatus = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                           VIRTIO_STATUS_FEATURES_OK;
    __asm__ volatile("mfence" ::: "memory");
    if ((Common->DeviceStatus & VIRTIO_STATUS_FEATURES_OK) == 0) {
        Common->DeviceStatus = VIRTIO_STATUS_FAILED;
        DebugWrite("Net: FEATURES_OK rejected\n");
        return -1;
    }
    if (VirtQueueSetup(&gRxQ, RX_QUEUE_ID, Common, NotifyBase, NotifyMult) != 0) {
        DebugWrite("Net: rx queue setup failed\n");
        return -1;
    }
    if (VirtQueueSetup(&gTxQ, TX_QUEUE_ID, Common, NotifyBase, NotifyMult) != 0) {
        DebugWrite("Net: tx queue setup failed\n");
        return -1;
    }

    /* Spec: configure + enable virtqueues before DRIVER_OK. */
    VirtQueueEnable(&gRxQ, RX_QUEUE_ID);
    VirtQueueEnable(&gTxQ, TX_QUEUE_ID);
    gRxQ.LastUsedIdx = gRxQ.Used->Idx;
    gTxQ.LastUsedIdx = gTxQ.Used->Idx;
    ReceiveRefillAll();

    Common->DeviceStatus = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                           VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
    __asm__ volatile("mfence" ::: "memory");

    /* Device ignores notifies until DRIVER_OK — kick RX again so buffers are live. */
    VirtQueueKick(&gRxQ, RX_QUEUE_ID);
    VirtQueueKick(&gTxQ, TX_QUEUE_ID);

    NetMemCpy(gMac, (const void *)DevCfg->Mac, 6);
    DebugWrite("Net: qsz rx=");
    DebugHex32(gRxQ.Size);
    DebugWrite(" tx=");
    DebugHex32(gTxQ.Size);
    DebugWrite(" bar4=");
    DebugHex64(ModernBar);
    DebugWrite("\n");
    return 0;
}
