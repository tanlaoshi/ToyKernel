/*
 * VirtioMmio.c — virtio-mmio（legacy v1 + modern v2）（PR-V3/V4）
 */
#include "VirtioMmio.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"

#define VIRTIO_MMIO_MAGIC               0x000u
#define VIRTIO_MMIO_VERSION             0x004u
#define VIRTIO_MMIO_DEVICE_ID           0x008u
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010u
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014u
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020u
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024u
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028u /* legacy */
#define VIRTIO_MMIO_QUEUE_SEL           0x030u
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034u
#define VIRTIO_MMIO_QUEUE_NUM           0x038u
#define VIRTIO_MMIO_QUEUE_ALIGN         0x03cu /* legacy */
#define VIRTIO_MMIO_QUEUE_PFN           0x040u /* legacy */
#define VIRTIO_MMIO_QUEUE_READY         0x044u /* modern */
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050u
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060u
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064u
#define VIRTIO_MMIO_STATUS              0x070u
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080u
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084u
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090u
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094u
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0a0u
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0a4u

#define VIRTIO_MAGIC_VALUE 0x74726976u

#ifndef VIRTIO_MMIO_BASE0
#define VIRTIO_MMIO_BASE0   0x0a000000ULL
#define VIRTIO_MMIO_STRIDE  0x200u
#define VIRTIO_MMIO_COUNT   32u
#endif

UINT32 VirtioMmioRead32(UINT64 Base, UINT32 Off) {
    return *(volatile UINT32 *)(UINTN)(Base + Off);
}

void VirtioMmioWrite32(UINT64 Base, UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(UINTN)(Base + Off) = Val;
}

UINT64 VirtioMmioRead64(UINT64 Base, UINT32 Off) {
    UINT32 Lo = VirtioMmioRead32(Base, Off);
    UINT32 Hi = VirtioMmioRead32(Base, Off + 4);
    return ((UINT64)Hi << 32) | (UINT64)Lo;
}

void VirtioMmioWrite64(UINT64 Base, UINT32 Off, UINT64 Val) {
    VirtioMmioWrite32(Base, Off, (UINT32)Val);
    VirtioMmioWrite32(Base, Off + 4, (UINT32)(Val >> 32));
}

int VirtioMmioScan(void (*Cb)(UINT64 Base, UINT32 DeviceId, void *Ctx), void *Ctx) {
    UINT32 i;
    int N = 0;

    if (!Cb) {
        return 0;
    }
    for (i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        UINT64 Base = VIRTIO_MMIO_BASE0 + (UINT64)i * (UINT64)VIRTIO_MMIO_STRIDE;
        UINT32 Magic = VirtioMmioRead32(Base, VIRTIO_MMIO_MAGIC);
        UINT32 Ver;
        UINT32 Id;

        if (Magic != VIRTIO_MAGIC_VALUE) {
            continue;
        }
        Ver = VirtioMmioRead32(Base, VIRTIO_MMIO_VERSION);
        Id = VirtioMmioRead32(Base, VIRTIO_MMIO_DEVICE_ID);
        if (Id == 0 || (Ver != 1 && Ver != 2)) {
            continue;
        }
        Cb(Base, Id, Ctx);
        N++;
    }
    return N;
}

static void Zero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

static void WireRingPtrs(VIRTIO_MMIO_DEV *Dev, UINT64 DescPa, UINT64 AvailPa,
                         UINT64 UsedPa) {
    Dev->Desc = (VRING_DESC *)(UINTN)DescPa;
    Dev->AvailFlags = (volatile UINT16 *)(UINTN)AvailPa;
    Dev->AvailIdx = (volatile UINT16 *)(UINTN)(AvailPa + 2);
    Dev->AvailRing = (volatile UINT16 *)(UINTN)(AvailPa + 4);
    Dev->UsedFlags = (volatile UINT16 *)(UINTN)UsedPa;
    Dev->UsedIdx = (volatile UINT16 *)(UINTN)(UsedPa + 2);
    Dev->UsedRing = (volatile VRING_USED_ELEM *)(UINTN)(UsedPa + 4);
}

int VirtioMmioSetupQueue(VIRTIO_MMIO_DEV *Dev, UINT64 Base, UINT32 DeviceId,
                         UINT16 WantSize, UINT64 DriverFeatures) {
    UINT32 Max;
    UINT16 Qsz;
    UINT8 *Pages;
    UINT32 Ver;
    UINT32 FeatLo;
    UINTN DescBytes;
    UINTN AvailBytes;
    UINTN UsedBytes;
    UINTN Align = PAGE_SIZE;
    UINT64 DescPa;
    UINT64 AvailPa;
    UINT64 UsedPa;
    UINT32 PagesN;

    if (!Dev || Base == 0) {
        return -1;
    }
    Zero(Dev, sizeof(*Dev));
    Dev->Base = Base;
    Dev->DeviceId = DeviceId;
    Ver = VirtioMmioRead32(Base, VIRTIO_MMIO_VERSION);

    VirtioMmioWrite32(Base, VIRTIO_MMIO_STATUS, 0);
    VirtioMmioWrite32(Base, VIRTIO_MMIO_STATUS,
                      VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    VirtioMmioWrite32(Base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    FeatLo = VirtioMmioRead32(Base, VIRTIO_MMIO_DEVICE_FEATURES);
    {
        UINT32 Take = FeatLo & (UINT32)DriverFeatures;
        VirtioMmioWrite32(Base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
        VirtioMmioWrite32(Base, VIRTIO_MMIO_DRIVER_FEATURES, Take);
    }
    if (Ver >= 2) {
        UINT32 FeatHi;
        VirtioMmioWrite32(Base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
        FeatHi = VirtioMmioRead32(Base, VIRTIO_MMIO_DEVICE_FEATURES);
        VirtioMmioWrite32(Base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
        VirtioMmioWrite32(Base, VIRTIO_MMIO_DRIVER_FEATURES,
                          FeatHi & (UINT32)(VIRTIO_F_VERSION_1 >> 32));
        VirtioMmioWrite32(Base, VIRTIO_MMIO_STATUS,
                          VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                              VIRTIO_STATUS_FEATURES_OK);
        if ((VirtioMmioRead32(Base, VIRTIO_MMIO_STATUS) &
             VIRTIO_STATUS_FEATURES_OK) == 0) {
            VirtioMmioWrite32(Base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
            return -1;
        }
    }

    if (Ver == 1) {
        VirtioMmioWrite32(Base, VIRTIO_MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);
    }

    VirtioMmioWrite32(Base, VIRTIO_MMIO_QUEUE_SEL, 0);
    Max = VirtioMmioRead32(Base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (Max == 0) {
        return -1;
    }
    Qsz = WantSize;
    if (Qsz > Max) {
        Qsz = (UINT16)Max;
    }
    if (Qsz < 4) {
        Qsz = (UINT16)((Max < 4) ? Max : 4);
    }
    Dev->QueueSize = Qsz;
    VirtioMmioWrite32(Base, VIRTIO_MMIO_QUEUE_NUM, Qsz);

    DescBytes = (UINTN)Qsz * sizeof(VRING_DESC);
    AvailBytes = 4u + (UINTN)Qsz * 2u + 2u;
    UsedBytes = 4u + (UINTN)Qsz * sizeof(VRING_USED_ELEM) + 2u;

    if (Ver == 1) {
        /* legacy：连续 vring，used 按 QueueAlign 对齐 */
        UINTN Size0 = DescBytes + AvailBytes;
        UINTN UsedOff = (Size0 + Align - 1) & ~(Align - 1);
        UINTN Total = UsedOff + UsedBytes;
        PagesN = (UINT32)((Total + PAGE_SIZE - 1) / PAGE_SIZE);
        if (PagesN < 2) {
            PagesN = 2;
        }
        Pages = (UINT8 *)PhysicalMemoryAllocatePages(PagesN);
        if (!Pages) {
            return -1;
        }
        Zero(Pages, (UINTN)PagesN * PAGE_SIZE);
        Dev->QueuePages = Pages;
        DescPa = (UINT64)(UINTN)Pages;
        AvailPa = DescPa + DescBytes;
        UsedPa = DescPa + UsedOff;
        WireRingPtrs(Dev, DescPa, AvailPa, UsedPa);
        VirtioMmioWrite32(Base, VIRTIO_MMIO_QUEUE_ALIGN, (UINT32)Align);
        VirtioMmioWrite32(Base, VIRTIO_MMIO_QUEUE_PFN,
                          (UINT32)(DescPa / PAGE_SIZE));
    } else {
        UINTN Need = DescBytes + AvailBytes + Align + UsedBytes;
        PagesN = (UINT32)((Need + PAGE_SIZE - 1) / PAGE_SIZE);
        if (PagesN < 2) {
            PagesN = 2;
        }
        Pages = (UINT8 *)PhysicalMemoryAllocatePages(PagesN);
        if (!Pages) {
            return -1;
        }
        Zero(Pages, (UINTN)PagesN * PAGE_SIZE);
        Dev->QueuePages = Pages;
        DescPa = (UINT64)(UINTN)Pages;
        AvailPa = (DescPa + DescBytes + 3ull) & ~3ull;
        UsedPa = (AvailPa + AvailBytes + (Align - 1)) & ~(UINT64)(Align - 1);
        WireRingPtrs(Dev, DescPa, AvailPa, UsedPa);
        VirtioMmioWrite64(Base, VIRTIO_MMIO_QUEUE_DESC_LOW, DescPa);
        VirtioMmioWrite64(Base, VIRTIO_MMIO_QUEUE_AVAIL_LOW, AvailPa);
        VirtioMmioWrite64(Base, VIRTIO_MMIO_QUEUE_USED_LOW, UsedPa);
        VirtioMmioWrite32(Base, VIRTIO_MMIO_QUEUE_READY, 1);
    }

    VirtioMmioWrite32(Base, VIRTIO_MMIO_STATUS,
                      VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                          (Ver >= 2 ? VIRTIO_STATUS_FEATURES_OK : 0) |
                          VIRTIO_STATUS_DRIVER_OK);

    Dev->NextAvail = 0;
    Dev->LastUsed = 0;
    return 0;
}

void VirtioMmioNotify(VIRTIO_MMIO_DEV *Dev) {
    if (!Dev) {
        return;
    }
    VirtioMmioWrite32(Dev->Base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
}

void VirtioMmioAckInterrupt(VIRTIO_MMIO_DEV *Dev) {
    UINT32 St;
    if (!Dev) {
        return;
    }
    St = VirtioMmioRead32(Dev->Base, VIRTIO_MMIO_INTERRUPT_STATUS);
    if (St) {
        VirtioMmioWrite32(Dev->Base, VIRTIO_MMIO_INTERRUPT_ACK, St);
    }
}
