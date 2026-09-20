/*
 * Net.c — 发送帧与地址查询（PR-S-net-1）
 */
#include "NetPrivate.h"
#include "Udp.h"
#include "Tcp.h"
#ifdef TOY_LWIP
#include "toy_netif.h"
#endif
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Serial.h"
#include "Debug.h"
#include "Hal.h"
#include "Driver.h"
#include "DriverNet.h"
#include "DriverNic.h"

int gNetOk;
UINT8 gMac[6];
UINT32 gIp = NET_IP_DEFAULT;
UINT32 gTxDone;
UINT32 gRxFrames;
int gLwIpRx;

void NetMemSet(void *Dst, UINT8 Val, UINTN Len) {
    UINT8 *P = (UINT8 *)Dst;
    while (Len--) {
        *P++ = Val;
    }
}

void NetMemCpy(void *Dst, const void *Src, UINTN Len) {
    UINT8 *D = (UINT8 *)Dst;
    const UINT8 *S = (const UINT8 *)Src;
    while (Len--) {
        *D++ = *S++;
    }
}

int NetSendFrame(const UINT8 *Frame, UINTN FrameLen) {
    UINT16 Head;
    UINT16 Slot;
    int Wait;
    UINTN WireLen;
    UINT64 IrqFlags;
    int NicResult;

    if (!gNetOk) {
        return -1;
    }
    NicResult = NetNicSendFrame(Frame, FrameLen);
    if (NicResult != -2) {
        return NicResult;
    }
    if (FrameLen + VIRTIO_NET_HDR_LEN > RX_BUF_SIZE) {
        return -1;
    }

    /* Pad to Ethernet minimum so short ARP frames are accepted. */
    WireLen = FrameLen < ETH_MIN_FRAME ? ETH_MIN_FRAME : FrameLen;

    /* 关中断：避免 syscall/Halt 期间被定时器抢到 shell 再进 NetPoll 打坏 vring */
    IrqFlags = HalIrqSave();

    /* Wait for previous TX descriptors to complete (single-buffer TX). */
    Wait = 100000;
    while (gTxQ.NumFree < gTxQ.Size && Wait-- > 0) {
        UINT16 DoneHead;
        UINT32 DoneLen;
        while (VirtQueuePopUsed(&gTxQ, &DoneHead, &DoneLen)) {
            if (DoneHead < gTxQ.Size) {
                VirtQueueFreeDescriptor(&gTxQ, DoneHead);
                gTxDone++;
            }
            (void)DoneLen;
        }
    }

    Head = VirtQueueAllocateDescriptor(&gTxQ);
    if (Head == (UINT16)~0 || Head >= gTxQ.Size) {
        HalIrqRestore(IrqFlags);
        return -1;
    }
    NetMemSet(gTxBuf, 0, VIRTIO_NET_HDR_LEN + WireLen);
    NetMemCpy(gTxBuf + VIRTIO_NET_HDR_LEN, Frame, FrameLen);
    gTxQ.Desc[Head].Addr = VirtualToPhysical(gTxBuf);
    gTxQ.Desc[Head].Len = (UINT32)(WireLen + VIRTIO_NET_HDR_LEN);
    gTxQ.Desc[Head].Flags = 0;
    gTxQ.Desc[Head].Next = 0;
    Slot = gTxQ.AvailIdx % gTxQ.Size;
    gTxQ.Avail->Ring[Slot] = Head;
    __asm__ volatile("mfence" ::: "memory");
    gTxQ.AvailIdx++;
    gTxQ.Avail->Idx = gTxQ.AvailIdx;
    VirtQueueKick(&gTxQ, TX_QUEUE_ID);
    HalIrqRestore(IrqFlags);
    return 0;
}

int NetSendEthernet(const UINT8 *Frame, UINTN Len) {
    if (Frame == 0 || Len < ETH_HDR_LEN) {
        return -1;
    }
    return NetSendFrame(Frame, Len);
}

void NetSetLwIpRx(int Enable) {
    gLwIpRx = Enable ? 1 : 0;
}

int NetLwIpRx(void) {
    return gLwIpRx;
}

UINT16 NetChecksum(const void *Data, UINTN Len) {
    return Sum16((const UINT8 *)Data, Len);
}

int NetInit(void) {
    (void)ToyDriverProbeClass(TOY_DRIVER_CLASS_NET);
    return 0;
}

int NetReady(void) {
    return gNetOk;
}

void NetGetStats(UINT32 *TxDone, UINT32 *RxFrames) {
    if (TxDone) {
        *TxDone = gTxDone;
    }
    if (RxFrames) {
        *RxFrames = gRxFrames;
    }
}

void NetGetMac(UINT8 Mac[6]) {
    NetMemCpy(Mac, gMac, 6);
}

UINT32 NetGetIp(void) {
    return gIp;
}

void NetSetIp(UINT32 Ip) {
    gIp = Ip;
}
