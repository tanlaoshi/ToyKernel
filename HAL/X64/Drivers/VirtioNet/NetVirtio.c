/*
 * NetVirtio.c — virtio-net 队列 / PCI 启动（PR-H-net-split-1）
 *
 * 从 Net.c 迁出；只搬家、不改逻辑。ARP/ICMP/对外 API 仍在 Net.c。
 */
#include "NetPrivate.h"
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

int VirtQueueSetup(VIRTQ *Q, UINT16 QueueId, volatile VIRTIO_COMMON_CFG *Common,
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

void VirtQueueEnable(VIRTQ *Q, UINT16 QueueId) {
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
        DebugWrite("Net: bad avail head\n");
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

void ReceiveRefillAll(void) {
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
