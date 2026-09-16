/*
 * NetVirtio.c — virtio-net 队列 / PCI 启动（PR-H-net-split-1）
 *
 * 从 Net.c 迁出；只搬家、不改逻辑。ARP/ICMP/对外 API 仍在 Net.c。
 */
#include "NetPriv.h"
#include "PCIe.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"


UINT8 gVirtQueueRxMemory[4 * PAGE_SIZE] __attribute__((aligned(4096)));
UINT8 gVirtQueueTxMemory[4 * PAGE_SIZE] __attribute__((aligned(4096)));
UINT8 gRxBufData[RX_BUF_COUNT][PAGE_SIZE] __attribute__((aligned(4096)));
static UINT8 gTxBufData[PAGE_SIZE] __attribute__((aligned(4096)));
UINT8 *gTxBuf = gTxBufData;

volatile VIRTIO_COMMON_CFG *gCommon;
volatile VIRTIO_NET_CFG *gDevCfg;
VIRTQ gRxQ;
VIRTQ gTxQ;
volatile UINT8 *gIsr;
UINT16 gIoPort;

void VirtQueueKick(VIRTQ *Q, UINT16 QueueId) {
    __asm__ volatile("mfence" ::: "memory");
    if (Q->NotifyBase != 0) {
        volatile UINT16 *Notify = (volatile UINT16 *)(Q->NotifyBase +
                                                        (UINTN)Q->NotifyOff * Q->NotifyMult);
        *Notify = QueueId;
    } else if (gIoPort != 0) {
        HalIoWrite16((UINT16)(gIoPort + 16), QueueId);
    }
    __asm__ volatile("mfence" ::: "memory");
}

static UINTN VirtQueueMemorySize(UINT16 Size) {
    UINTN Desc = Size * sizeof(VRING_DESC);
    UINTN Avail = sizeof(UINT16) * (2 + Size);
    UINTN Used = sizeof(UINT16) * 2 + Size * sizeof(VRING_USED_ELEM);
    UINTN Off = Desc + Avail;
    Off = (Off + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    return Off + Used;
}

static int VirtQueueSetup(VIRTQ *Q, UINT16 QueueId, volatile VIRTIO_COMMON_CFG *Common,
                   volatile UINT8 *NotifyBase, UINT32 NotifyMult) {
    UINT8 *Mem;
    UINTN MemSize;
    UINTN UsedOff;
    int i;

    Q->Common = Common;
    Q->NotifyBase = NotifyBase;
    Q->NotifyMult = NotifyMult;
    Common->QueueSelect = QueueId;
    Q->Size = Common->QueueSize;
    if (Q->Size == 0) {
        return -1;
    }
    MemSize = VirtQueueMemorySize(Q->Size);
    if (QueueId == RX_QUEUE_ID) {
        Mem = gVirtQueueRxMemory;
    } else {
        Mem = gVirtQueueTxMemory;
    }
    if (MemSize > 4 * PAGE_SIZE) {
        return -1;
    }
    NetMemSet(Mem, 0, MemSize);
    Q->Desc = (VRING_DESC *)Mem;
    Q->Avail = (VRING_AVAIL *)(Mem + Q->Size * sizeof(VRING_DESC));
    UsedOff = ((UINTN)Q->Avail + sizeof(UINT16) * (2 + Q->Size) + PAGE_SIZE - 1) &
              ~(PAGE_SIZE - 1);
    Q->Used = (VRING_USED *)(Mem + UsedOff);
    Q->AvailIdx = 0;
    Q->LastUsedIdx = 0;
    Q->FreeHead = 0;
    Q->NumFree = Q->Size;
    for (i = 0; i < (int)Q->Size - 1; i++) {
        Q->Desc[i].Next = (UINT16)(i + 1);
    }
    Q->NotifyOff = Common->QueueNotifyOff;
    Common->QueueDesc = VirtualToPhysical(Q->Desc);
    Common->QueueDriver = VirtualToPhysical(Q->Avail);
    Common->QueueDevice = VirtualToPhysical(Q->Used);
    __asm__ volatile("" ::: "memory");
    return 0;
}

static void VirtQueueEnable(VIRTQ *Q, UINT16 QueueId) {
    Q->Common->QueueSelect = QueueId;
    Q->NotifyOff = Q->Common->QueueNotifyOff;
    __asm__ volatile("" ::: "memory");
    Q->Common->QueueEnable = 1;
}

UINT16 VirtQueueAllocateDescriptor(VIRTQ *Q) {
    UINT16 Idx;
    if (Q->NumFree == 0) {
        return (UINT16)~0;
    }
    Idx = Q->FreeHead;
    Q->FreeHead = Q->Desc[Idx].Next;
    Q->NumFree--;
    Q->Desc[Idx].Flags = 0;
    Q->Desc[Idx].Next = 0;
    return Idx;
}

void VirtQueueFreeDescriptor(VIRTQ *Q, UINT16 Idx) {
    Q->Desc[Idx].Next = Q->FreeHead;
    Q->FreeHead = Idx;
    Q->NumFree++;
}

static void VirtQueueSubmitAvailable(VIRTQ *Q, UINT16 Head) {
    UINT16 Slot;

    if (Head >= Q->Size) {
        DebugWrite("net: bad avail head\n");
        return;
    }
    Slot = Q->AvailIdx % Q->Size;
    Q->Avail->Ring[Slot] = Head;
    __asm__ volatile("" ::: "memory");
    Q->AvailIdx++;
    Q->Avail->Idx = Q->AvailIdx;
    VirtQueueKick(Q, Q == &gRxQ ? RX_QUEUE_ID : TX_QUEUE_ID);
}

int VirtQueuePopUsed(VIRTQ *Q, UINT16 *Head, UINT32 *Len) {
    UINT16 UsedIdx;

    __asm__ volatile("" ::: "memory");
    UsedIdx = Q->Used->Idx;
    if (Q->LastUsedIdx == UsedIdx) {
        return 0;
    }
    {
        UINT16 Idx = Q->LastUsedIdx % Q->Size;
        VRING_USED_ELEM *Elem = &Q->Used->Ring[Idx];
        *Head = (UINT16)Elem->Id;
        *Len = Elem->Len;
        Q->LastUsedIdx++;
    }
    return 1;
}

void ReceiveRefillOne(VIRTQ *Q, UINT8 *Buf) {
    UINT16 Head = VirtQueueAllocateDescriptor(Q);
    if (Head == (UINT16)~0) {
        return;
    }
    Q->Desc[Head].Addr = VirtualToPhysical(Buf);
    Q->Desc[Head].Len = RX_BUF_SIZE;
    Q->Desc[Head].Flags = VRING_DESC_F_WRITE;
    VirtQueueSubmitAvailable(Q, Head);
}

static void ReceiveRefillAll(void) {
    int i;
    for (i = 0; i < RX_BUF_COUNT; i++) {
        ReceiveRefillOne(&gRxQ, gRxBufData[i]);
    }
}
int VirtioFindNet(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut) {
    int B;
    int D;
    int F;

    for (B = 0; B < 256; B++) {
        for (D = 0; D < 32; D++) {
            for (F = 0; F < 8; F++) {
                UINT32 VidDid = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT16 Vid = (UINT16)(VidDid & 0xFFFF);
                UINT16 Did = (UINT16)(VidDid >> 16);
                UINT32 BarLo;

                if (Vid != VIRTIO_VENDOR_ID || Did != VIRTIO_DEV_NET) {
                    continue;
                }
                *Bus = (UINT8)B;
                *Dev = (UINT8)D;
                *Fn = (UINT8)F;
                BarLo = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x10);
                *BarOut = BarLo & 0xFFFFFFF0ULL;
                return 1;
            }
        }
    }
    return 0;
}

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
        DebugWrite("net: missing virtio pci caps\n");
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
        DebugWrite("net: FEATURES_OK rejected\n");
        return -1;
    }
    if (VirtQueueSetup(&gRxQ, RX_QUEUE_ID, Common, NotifyBase, NotifyMult) != 0) {
        DebugWrite("net: rx queue setup failed\n");
        return -1;
    }
    if (VirtQueueSetup(&gTxQ, TX_QUEUE_ID, Common, NotifyBase, NotifyMult) != 0) {
        DebugWrite("net: tx queue setup failed\n");
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
    DebugWrite("net: qsz rx=");
    DebugHex32(gRxQ.Size);
    DebugWrite(" tx=");
    DebugHex32(gTxQ.Size);
    DebugWrite(" bar4=");
    DebugHex64(ModernBar);
    DebugWrite("\n");
    return 0;
}
