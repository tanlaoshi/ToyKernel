/*
 * VirtioMmio.h — QEMU virt virtio-mmio（modern v2）公共层（PR-V3/V4）
 */
#ifndef HAL_VIRTIO_MMIO_H
#define HAL_VIRTIO_MMIO_H

#include "BootTypes.h"

#define VIRTIO_DEV_BLOCK 2u
#define VIRTIO_DEV_INPUT 18u
#define VIRTIO_DEV_NET   1u

#define VIRTIO_STATUS_ACK         1u
#define VIRTIO_STATUS_DRIVER      2u
#define VIRTIO_STATUS_DRIVER_OK   4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED      128u

#define VIRTIO_F_VERSION_1 (1ULL << 32)

#define VRING_DESC_F_NEXT  1u
#define VRING_DESC_F_WRITE 2u

typedef struct {
    UINT64 Addr;
    UINT32 Len;
    UINT16 Flags;
    UINT16 Next;
} __attribute__((packed)) VRING_DESC;

typedef struct {
    UINT16 Flags;
    UINT16 Idx;
    UINT16 Ring[0];
} __attribute__((packed)) VRING_AVAIL;

typedef struct {
    UINT32 Id;
    UINT32 Len;
} __attribute__((packed)) VRING_USED_ELEM;

typedef struct {
    UINT16 Flags;
    UINT16 Idx;
    VRING_USED_ELEM Ring[0];
} __attribute__((packed)) VRING_USED;

typedef struct {
    UINT64 Base; /* MMIO */
    UINT32 DeviceId;
    UINT16 QueueSize;
    VRING_DESC *Desc;
    volatile UINT16 *AvailFlags;
    volatile UINT16 *AvailIdx;
    volatile UINT16 *AvailRing;
    volatile UINT16 *UsedFlags;
    volatile UINT16 *UsedIdx;
    volatile VRING_USED_ELEM *UsedRing;
    UINT16 NextAvail;
    UINT16 LastUsed;
    void *QueuePages;
} VIRTIO_MMIO_DEV;

UINT32 VirtioMmioRead32(UINT64 Base, UINT32 Off);
void VirtioMmioWrite32(UINT64 Base, UINT32 Off, UINT32 Val);
UINT64 VirtioMmioRead64(UINT64 Base, UINT32 Off);
void VirtioMmioWrite64(UINT64 Base, UINT32 Off, UINT64 Val);

/* 扫描平台 virtio-mmio 槽；对每个非 0 DeviceId 调 Cb。返回发现设备数 */
int VirtioMmioScan(void (*Cb)(UINT64 Base, UINT32 DeviceId, void *Ctx), void *Ctx);

/* 复位 + ACK/DRIVER + 特性协商（FEATURES_OK）；不建队列、不 DRIVER_OK */
int VirtioMmioNegotiate(VIRTIO_MMIO_DEV *Dev, UINT64 Base, UINT32 DeviceId,
                        UINT64 DriverFeatures);

/* 在已 Negotiate 的设备上配置 QueueId（可多次）；不置 DRIVER_OK */
int VirtioMmioSetupOneQueue(VIRTIO_MMIO_DEV *Dev, UINT16 QueueId, UINT16 WantSize);

void VirtioMmioDriverOk(VIRTIO_MMIO_DEV *Dev);

/* 完成 ACK/DRIVER/FEATURES_OK + 单队列（QID=0）就绪；成功 0 */
int VirtioMmioSetupQueue(VIRTIO_MMIO_DEV *Dev, UINT64 Base, UINT32 DeviceId,
                         UINT16 WantSize, UINT64 DriverFeatures);

void VirtioMmioNotify(VIRTIO_MMIO_DEV *Dev);
void VirtioMmioNotifyQueue(VIRTIO_MMIO_DEV *Dev, UINT16 QueueId);
void VirtioMmioAckInterrupt(VIRTIO_MMIO_DEV *Dev);

#endif
