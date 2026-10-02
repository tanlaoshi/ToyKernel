/*
 * IwlFw.c — 读盘 / TLV / INIT→RT 编排（PR-N-wifi-2）
 */
#include "IwlPrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "ToySerialLog.h"

UINT8 *gIwlFwBlob;
UINTN gIwlFwBlobSize;
UINTN gIwlFwSize;
IWL_FW_IMG gIwlImgRt;
IWL_FW_IMG gIwlImgInit;
UINT32 gIwlPhyCfg;
UINT32 gIwlCalibFlow[IWL_UCODE_TYPE_MAX];
UINT32 gIwlCalibEvent[IWL_UCODE_TYPE_MAX];

static void IwlZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

static int IwlFwReadBlob(void) {
    UINT8 Probe[64];
    UINTN Sz = 0;
    UINTN Got = 0;
    UINTN Pages;
    void *Mem;
    int Err;

    gIwlFwOk = 0;
    gIwlFwSize = 0;
    Err = FileSystemReadFile(IWL_FW_PATH, Probe, sizeof(Probe), &Sz);
    if (Err != FAT_OK || Sz == 0) {
        ToyLogDrv("Boot: iwl8265 fw miss (FW/IWL8265.UCODE)\n");
        return 0;
    }
    Pages = (2500u * 1024u + PAGE_SIZE - 1) / PAGE_SIZE;
    Mem = PhysicalMemoryAllocatePages((UINT32)Pages);
    if (!Mem) {
        ToyLogDrv("Boot: iwl8265 fw oom\n");
        return 0;
    }
    IwlZero(Mem, Pages * PAGE_SIZE);
    Err = FileSystemReadFile(IWL_FW_PATH, Mem, Pages * PAGE_SIZE, &Got);
    if (Err != FAT_OK || Got < 128) {
        ToyLogDrv("Boot: iwl8265 fw read fail\n");
        return 0;
    }
    gIwlFwBlob = (UINT8 *)Mem;
    gIwlFwBlobSize = Got;
    gIwlFwSize = Got;
    gIwlFwOk = 1;
    return 1;
}

static int IwlFwAddSec(IWL_FW_IMG *Img, const UINT8 *Data, UINT32 Len) {
    UINT32 Off;

    if (!Img || Img->NumSec >= IWL_FW_SEC_MAX || Len < 4) {
        return 0;
    }
    Off = Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16)
        | ((UINT32)Data[3] << 24);
    Img->Sec[Img->NumSec].Offset = Off;
    Img->Sec[Img->NumSec].Data = Data + 4;
    Img->Sec[Img->NumSec].Len = Len - 4;
    Img->NumSec++;
    if (Off == IWL_CPU1_CPU2_SEP) {
        Img->DualCpus = 1;
    }
    return 1;
}

static int IwlFwParseTlv(void) {
    UINTN Off;
    UINT32 Magic;

    IwlZero(&gIwlImgRt, sizeof(gIwlImgRt));
    IwlZero(&gIwlImgInit, sizeof(gIwlImgInit));
    if (!gIwlFwBlob || gIwlFwBlobSize < 88) {
        return 0;
    }
    Magic = gIwlFwBlob[4] | ((UINT32)gIwlFwBlob[5] << 8)
          | ((UINT32)gIwlFwBlob[6] << 16) | ((UINT32)gIwlFwBlob[7] << 24);
    if (Magic != IWL_TLV_MAGIC) {
        ToyLogDrv("Boot: iwl8265 fw magic bad\n");
        return 0;
    }
    Off = 88;
    while (Off + 8 <= gIwlFwBlobSize) {
        UINT32 Type = gIwlFwBlob[Off] | ((UINT32)gIwlFwBlob[Off + 1] << 8)
                    | ((UINT32)gIwlFwBlob[Off + 2] << 16)
                    | ((UINT32)gIwlFwBlob[Off + 3] << 24);
        UINT32 Len = gIwlFwBlob[Off + 4] | ((UINT32)gIwlFwBlob[Off + 5] << 8)
                   | ((UINT32)gIwlFwBlob[Off + 6] << 16)
                   | ((UINT32)gIwlFwBlob[Off + 7] << 24);
        const UINT8 *Payload;

        Off += 8;
        if (Off + Len > gIwlFwBlobSize) {
            break;
        }
        Payload = gIwlFwBlob + Off;
        if (Type == IWL_TLV_SEC_RT) {
            (void)IwlFwAddSec(&gIwlImgRt, Payload, Len);
        } else if (Type == IWL_TLV_SEC_INIT) {
            (void)IwlFwAddSec(&gIwlImgInit, Payload, Len);
        } else if (Type == IWL_TLV_PHY_SKU && Len >= 4) {
            gIwlPhyCfg = Payload[0] | ((UINT32)Payload[1] << 8)
                       | ((UINT32)Payload[2] << 16)
                       | ((UINT32)Payload[3] << 24);
        } else if (Type == IWL_TLV_DEF_CALIB && Len >= 12) {
            UINT32 Utype = Payload[0] | ((UINT32)Payload[1] << 8)
                         | ((UINT32)Payload[2] << 16)
                         | ((UINT32)Payload[3] << 24);
            if (Utype < IWL_UCODE_TYPE_MAX) {
                gIwlCalibFlow[Utype] =
                    Payload[4] | ((UINT32)Payload[5] << 8)
                    | ((UINT32)Payload[6] << 16)
                    | ((UINT32)Payload[7] << 24);
                gIwlCalibEvent[Utype] =
                    Payload[8] | ((UINT32)Payload[9] << 8)
                    | ((UINT32)Payload[10] << 16)
                    | ((UINT32)Payload[11] << 24);
            }
        } else if (Type == IWL_TLV_PAGING && Len >= 4) {
            /* OpenBSD/Linux：挂在 REGULAR(RT)；INIT 镜像里也有 SEP 段 */
            gIwlImgRt.PagingMemSize =
                Payload[0] | ((UINT32)Payload[1] << 8)
                | ((UINT32)Payload[2] << 16)
                | ((UINT32)Payload[3] << 24);
        }
        Off += (Len + 3u) & ~3u;
    }
    return gIwlImgRt.NumSec > 0 && gIwlImgInit.NumSec > 0;
}

static void IwlFwLoadPrep(void) {
    IwlMmioW32(IWL_CSR_INT, 0xFFFFFFFF);
    IwlMmioW32(IWL_CSR_INT_MASK, IWL_CSR_INT_FH_TX);
    IwlMmioW32(IWL_CSR_UCODE_DRV_GP1_CLR, IWL_CSR_UCODE_SW_RFKILL);
    IwlMmioW32(IWL_CSR_UCODE_DRV_GP1_CLR, IWL_CSR_UCODE_CMD_BLOCKED);
}

static void IwlFwEnableAliveInts(void) {
    IwlMmioW32(IWL_CSR_INT_MASK,
               IWL_CSR_INT_FH_TX | IWL_CSR_INT_FH_RX | IWL_CSR_INT_ALIVE);
}

int IwlFwTryLoad(void) {
    return IwlFwReadBlob();
}

/* 1=ok；0=alive=fail；-1=fwload=fail；-2=init_alive=fail */
int IwlFwParseAndLoad(void) {
    if (!gIwlFwOk && !IwlFwReadBlob()) {
        return -1;
    }
    if (!IwlFwParseTlv()) {
        return -1;
    }

    IwlFwLoadPrep();
    if (!IwlLoadUcode8000(&gIwlImgInit)) {
        return -1;
    }
    /* Load 时停了 RX：重绑环再等 ALIVE */
    if (!IwlRxInit()) {
        return -1;
    }
    IwlFwEnableAliveInts();
    if (!IwlWaitAlive()) {
        return -2;
    }
    /* 刀 #57：INIT 上跑校准，收 phy_db 段，供 RT SCAN_CFG 前下发 */
    if (IwlPostAlive()) {
        if (!IwlPhyInitCalib()) {
            IwlLogStage("initcal=miss");
        }
    } else {
        IwlLogStage("initcal=scd");
    }

    gIwlAlive = 0;
    if (IwlHwStart() != 1 || !IwlNicInit()) {
        return -1;
    }

    IwlFwLoadPrep();
    if (!IwlLoadUcode8000(&gIwlImgRt)) {
        return -1;
    }
    if (!IwlRxInit()) {
        return -1;
    }
    IwlFwEnableAliveInts();
    if (!IwlWaitAlive()) {
        return 0;
    }
    if (!IwlPostAlive()) {
        return 0;
    }
    /* 刀 #39：OpenBSD 在 post_alive 后立刻 paging，再发业务命令 */
    if (!IwlPagingInit(&gIwlImgRt)) {
        IwlLogStage("page=earlyfail");
        /* 软失败：仍试后续，对照日志 */
    }
    return 1;
}
