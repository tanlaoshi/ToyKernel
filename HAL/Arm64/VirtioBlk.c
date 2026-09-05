/*
 * VirtioBlk.c — virtio-blk MMIO（PR-V4；PR-D2 经 Drv Block 类）
 */
#include "VirtioBlk.h"
#include "VirtioMmio.h"
#include "HalSerial.h"
#include "PhysicalMemory.h"
#include "Drv.h"
#include "DrvBlock.h"

#define VIRTIO_BLK_T_IN  0u
#define VIRTIO_BLK_T_OUT 1u
#define VIRTIO_BLK_S_OK  0u

typedef struct {
    UINT32 Type;
    UINT32 Reserved;
    UINT64 Sector;
} __attribute__((packed)) VIRTIO_BLK_REQ;

static VIRTIO_MMIO_DEV gBlk;
static int gBlkReady;
static UINT8 *gStatusByte;
static VIRTIO_BLK_REQ *gReq;
static UINT8 *gIoBuf; /* 最多 8 扇区 bounce */
#define VIRTIO_BLK_MAX_SECTORS 8u

static void HexU32(UINT32 V) {
    static const char Hex[] = "0123456789abcdef";
    char Buf[9];
    int i;
    for (i = 7; i >= 0; i--) {
        Buf[i] = Hex[V & 0xf];
        V >>= 4;
    }
    Buf[8] = 0;
    HalSerialWrite(Buf);
}

static int BlkXfer(UINT32 Type, UINT32 Lba, UINT32 Count, void *Buffer) {
    UINT16 D0;
    UINT16 D1;
    UINT16 D2;
    UINT16 A;
    UINT16 Want;
    UINTN Bytes;
    UINT8 *Data = (UINT8 *)Buffer;
    UINT32 Done = 0;

    if (!gBlkReady || !Buffer || Count == 0) {
        return 0;
    }

    while (Done < Count) {
        UINT32 Chunk = Count - Done;
        if (Chunk > VIRTIO_BLK_MAX_SECTORS) {
            Chunk = VIRTIO_BLK_MAX_SECTORS;
        }
        Bytes = (UINTN)Chunk * 512u;

        gReq->Type = Type;
        gReq->Reserved = 0;
        gReq->Sector = (UINT64)Lba + (UINT64)Done;
        *gStatusByte = 0xff;

        if (Type == VIRTIO_BLK_T_OUT) {
            UINTN i;
            for (i = 0; i < Bytes; i++) {
                gIoBuf[i] = Data[(UINTN)Done * 512u + i];
            }
        }

        D0 = 0;
        D1 = 1;
        D2 = 2;
        gBlk.Desc[D0].Addr = (UINT64)(UINTN)gReq;
        gBlk.Desc[D0].Len = sizeof(VIRTIO_BLK_REQ);
        gBlk.Desc[D0].Flags = VRING_DESC_F_NEXT;
        gBlk.Desc[D0].Next = D1;

        gBlk.Desc[D1].Addr = (UINT64)(UINTN)gIoBuf;
        gBlk.Desc[D1].Len = (UINT32)Bytes;
        gBlk.Desc[D1].Flags = VRING_DESC_F_NEXT |
                              ((Type == VIRTIO_BLK_T_IN) ? VRING_DESC_F_WRITE : 0);
        gBlk.Desc[D1].Next = D2;

        gBlk.Desc[D2].Addr = (UINT64)(UINTN)gStatusByte;
        gBlk.Desc[D2].Len = 1;
        gBlk.Desc[D2].Flags = VRING_DESC_F_WRITE;
        gBlk.Desc[D2].Next = 0;

        A = gBlk.NextAvail;
        gBlk.AvailRing[A % gBlk.QueueSize] = D0;
        __asm__ volatile("" ::: "memory");
        *gBlk.AvailIdx = (UINT16)(A + 1);
        gBlk.NextAvail = (UINT16)(A + 1);
        VirtioMmioNotify(&gBlk);

        Want = gBlk.LastUsed;
        while (*gBlk.UsedIdx == Want) {
            VirtioMmioAckInterrupt(&gBlk);
            __asm__ volatile("" ::: "memory");
        }
        gBlk.LastUsed = (UINT16)(Want + 1);
        VirtioMmioAckInterrupt(&gBlk);

        if (*gStatusByte != VIRTIO_BLK_S_OK) {
            return 0;
        }
        if (Type == VIRTIO_BLK_T_IN) {
            UINTN i;
            for (i = 0; i < Bytes; i++) {
                Data[(UINTN)Done * 512u + i] = gIoBuf[i];
            }
        }
        Done += Chunk;
    }
    return 1;
}

static int BlkProbe(UINT32 Drive) {
    return (Drive == 0 && gBlkReady) ? 1 : 0;
}

static int BlkRead(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    (void)Drive;
    return BlkXfer(VIRTIO_BLK_T_IN, Lba, Count, Buffer);
}

static int BlkWrite(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    (void)Drive;
    return BlkXfer(VIRTIO_BLK_T_OUT, Lba, Count, (void *)Buffer);
}

static const BLOCK_BACKEND gBlkBackend = {
    .Probe = BlkProbe,
    .ReadSectors = BlkRead,
    .WriteSectors = BlkWrite,
    .Flush = 0,
};

typedef struct {
    UINT64 FoundBase;
} BLK_SCAN_CTX;

static void BlkScanCb(UINT64 Base, UINT32 DeviceId, void *Ctx) {
    BLK_SCAN_CTX *S = (BLK_SCAN_CTX *)Ctx;
    if (DeviceId == VIRTIO_DEV_BLOCK && S->FoundBase == 0) {
        S->FoundBase = Base;
    }
}

static int VirtioBlkDrvProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    BLK_SCAN_CTX Ctx;
    UINT8 *Meta;

    (void)Self;
    (void)BusCtx;
    if (gBlkReady) {
        if (OutPriv) {
            *OutPriv = 0;
        }
        return 0;
    }

    Ctx.FoundBase = 0;
    VirtioMmioScan(BlkScanCb, &Ctx);
    if (Ctx.FoundBase == 0) {
        return -1;
    }

    if (VirtioMmioSetupQueue(&gBlk, Ctx.FoundBase, VIRTIO_DEV_BLOCK, 8, 0) != 0) {
        HalSerialWrite("virtio-blk: setup failed\n");
        return -1;
    }

    Meta = (UINT8 *)PhysicalMemoryAllocatePages(1);
    if (!Meta) {
        return -1;
    }
    gReq = (VIRTIO_BLK_REQ *)(UINTN)Meta;
    gStatusByte = Meta + 64;
    gIoBuf = Meta + 128;
    gBlkReady = 1;

    HalSerialWrite("boot: virtio-blk @");
    HexU32((UINT32)Ctx.FoundBase);
    HalSerialWrite("\n");

    if (OutPriv) {
        *OutPriv = 0;
    }
    return 0;
}

static int VirtioBlkDrvBind(TOY_DRV_INSTANCE *Inst) {
    (void)Inst;
    return ToyDrvBlockAttach(&gBlkBackend);
}

static void VirtioBlkDrvRemove(TOY_DRV_INSTANCE *Inst) {
    (void)Inst;
    gBlkReady = 0;
}

static const TOY_DRIVER gVirtioBlkDriver = {
    .Name = "virtio-blk",
    .Class = TOY_DRV_CLASS_BLOCK,
    .Match = 0,
    .Probe = VirtioBlkDrvProbe,
    .Bind = VirtioBlkDrvBind,
    .Remove = VirtioBlkDrvRemove,
};

void VirtioBlkRegister(void) {
    (void)ToyDrvRegister(&gVirtioBlkDriver);
}

int VirtioBlkInit(void) {
    (void)ToyDrvProbeClass(TOY_DRV_CLASS_BLOCK);
    if (!BlockBackendReady()) {
        return 0;
    }
    return BlockInit();
}
