/*
 * NetPrivate.h — Net / NetVirtio 内部共享（仅 HAL/X64/Drivers/Net*.c）
 *
 * 禁止 Common / User 包含；对外用 Net.h / HalDevices。
 * PR-H-net-split-1：与 NetVirtio.c 一并引入。
 */
#ifndef NET_PRIVATE_H
#define NET_PRIVATE_H

#include "Net.h"
#include "BootTypes.h"
#include "PhysicalMemory.h"
#include "Hal.h"

#define VIRTIO_VENDOR_ID      0x1AF4
#define VIRTIO_DEV_NET        0x1000
#define PCI_CAP_VENDOR        0x09
#define VIRTIO_PCI_CAP_COMMON 1
#define VIRTIO_PCI_CAP_ISR    3
#define VIRTIO_PCI_CAP_DEVICE 4
#define VIRTIO_PCI_CAP_NOTIFY 2

#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED    128

#define VIRTIO_F_VERSION_1      (1ULL << 32)
#define VIRTIO_NET_F_MAC        (1ULL << 5)

#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2

#define VIRTIO_NET_HDR_LEN      12
#define ETH_HDR_LEN             14
#define ETH_MIN_FRAME           60
#define IP_HDR_LEN              20
#define ICMP_HDR_LEN            8

#define ETH_TYPE_ARP            0x0806
#define ETH_TYPE_IP             0x0800
#define ARP_OP_REQUEST          1
#define ARP_OP_REPLY            2
#define IP_PROTO_ICMP           1
#define IP_PROTO_TCP            6
#define IP_PROTO_UDP            17
#define ICMP_ECHO_REQUEST       8
#define ICMP_ECHO_REPLY         0

#define RX_QUEUE_ID             0
#define TX_QUEUE_ID             1
#define RX_BUF_SIZE             2048
#define RX_BUF_COUNT            16
#define ARP_CACHE_SIZE          4
#define PING_PAYLOAD            32

typedef struct {
    UINT64 Addr;
    UINT32 Len;
    UINT16 Flags;
    UINT16 Next;
} __attribute__((packed)) VRING_DESC;

typedef struct {
    UINT16 Flags;
    UINT16 Idx;
    UINT16 Ring[];
} VRING_AVAIL;

typedef struct {
    UINT32 Id;
    UINT32 Len;
} __attribute__((packed)) VRING_USED_ELEM;

typedef struct {
    UINT16 Flags;
    UINT16 Idx;
    VRING_USED_ELEM Ring[];
} VRING_USED;

typedef struct {
    UINT32 DeviceFeatureSelect;
    UINT32 DeviceFeature;
    UINT32 DriverFeatureSelect;
    UINT32 DriverFeature;
    UINT16 MsixConfig;
    UINT16 NumQueues;
    UINT8  DeviceStatus;
    UINT8  ConfigGeneration;
    UINT16 QueueSelect;
    UINT16 QueueSize;
    UINT16 QueueMsixVector;
    UINT16 QueueEnable;
    UINT16 QueueNotifyOff;
    UINT64 QueueDesc;
    UINT64 QueueDriver;
    UINT64 QueueDevice;
} __attribute__((packed)) VIRTIO_COMMON_CFG;

typedef struct {
    UINT8 Mac[6];
    UINT16 Status;
} __attribute__((packed)) VIRTIO_NET_CFG;

typedef struct {
    UINT16 Size;
    UINT16 LastUsedIdx;
    UINT16 FreeHead;
    UINT16 NumFree;
    VRING_DESC *Desc;
    VRING_AVAIL *Avail;
    VRING_USED *Used;
    UINT16 AvailIdx;
    UINT16 NotifyOff;
    volatile VIRTIO_COMMON_CFG *Common;
    volatile UINT8 *NotifyBase;
    UINT32 NotifyMult;
} VIRTQ;

/* 协议头（ARP/IP/ICMP/ETH）留在 Net.c；队列态在 NetVirtio.c */
extern VIRTQ gRxQ;
extern VIRTQ gTxQ;
extern UINT8 gRxBufData[RX_BUF_COUNT][PAGE_SIZE];
extern UINT8 *gTxBuf;
extern volatile UINT8 *gIsr;
extern UINT16 gIoPort;

extern int gNetOk;
extern const NIC_L2 *gNicL2;
extern UINT8 gMac[6];
extern UINT32 gIp;
extern UINT32 gTxDone;
extern UINT32 gRxFrames;
extern int gLwIpRx;

void NetMemSet(void *Dst, UINT8 Val, UINTN Len);
void NetMemCpy(void *Dst, const void *Src, UINTN Len);

/* Net.c：挂上本文件的 NET_BACKEND */
int NetProtocolAttach(void);
int NetNicSendFrame(const UINT8 *Frame, UINTN FrameLen);
void NetNicPoll(void);
int NetNicHasL2(void);

static inline UINT64 VirtualToPhysical(void *Ptr) {
    return (UINT64)(UINTN)Ptr;
}


void VirtQueueKick(VIRTQ *Q, UINT16 QueueId);
UINT16 VirtQueueAllocateDescriptor(VIRTQ *Q);
void VirtQueueFreeDescriptor(VIRTQ *Q, UINT16 Idx);
int VirtQueuePopUsed(VIRTQ *Q, UINT16 *Head, UINT32 *Len);
void ReceiveRefillOne(VIRTQ *Q, UINT8 *Buf);

int VirtioFindNet(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut);
int VirtioNetStart(UINT8 Bus, UINT8 Dev, UINT8 Fn, UINT64 BarPhys);

#endif
