/*
 * IwlPaging.c — CPU2 FW paging（刀 #24）
 *
 * 8265 TLV_PAGING=159744；PAGING_SEP 后 CSS(4K)+镜像。UMAC 依赖此 DRAM。
 * 对照 Linux iwlwifi/fw/paging.c、OpenBSD iwm_send_paging_cmd。
 */
#include "IwlPrivate.h"
#include "PhysicalMemory.h"

typedef struct {
    UINT8 *Virt;
    UINT64 Phys;
    UINT32 Size;
} IWL_PAGING_BLK;

static IWL_PAGING_BLK gPageDb[IWL_NUM_FW_PAGING_BLOCKS];
static UINT32 gNumPagingBlk;
static UINT32 gPagesInLast;
static int gPagingReady;

static void IwlZero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    UINTN i;
    for (i = 0; i < N; i++) {
        B[i] = 0;
    }
}

static void IwlCopy(void *D, const void *S, UINTN N) {
    UINT8 *d = (UINT8 *)D;
    const UINT8 *s = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        d[i] = s[i];
    }
}

static int IwlPagingAlloc(UINT32 PagingMemSize) {
    UINT32 NumPages;
    UINT32 Blk;
    UINT32 Size;
    UINT32 Align;
    UINT32 Pages;
    UINT32 Extra;
    void *Mem;
    UINTN Addr;
    UINTN Aligned;

    if (gPagingReady) {
        return 1;
    }
    if (PagingMemSize == 0
        || PagingMemSize > IWL_MAX_PAGING_IMAGE_SIZE
        || (PagingMemSize & (IWL_FW_PAGING_SIZE - 1u)) != 0) {
        return 0;
    }

    NumPages = PagingMemSize / IWL_FW_PAGING_SIZE;
    gNumPagingBlk = (NumPages + IWL_NUM_PAGE_PER_GROUP - 1u) / IWL_NUM_PAGE_PER_GROUP;
    gPagesInLast = NumPages
                 - IWL_NUM_PAGE_PER_GROUP * (gNumPagingBlk - 1u);
    if (gNumPagingBlk == 0 || gNumPagingBlk >= IWL_NUM_FW_PAGING_BLOCKS) {
        return 0;
    }

    IwlZero(gPageDb, sizeof(gPageDb));
    for (Blk = 0; Blk < gNumPagingBlk + 1u; Blk++) {
        /* Linux buddy：CSS 4K 对齐；data 块 32K 对齐。PMM 只保 4K。 */
        Size = Blk ? IWL_PAGING_BLOCK_SIZE : IWL_FW_PAGING_SIZE;
        Align = Size;
        Pages = (Size + PAGE_SIZE - 1u) / PAGE_SIZE;
        Extra = (Align / PAGE_SIZE) > 0 ? (Align / PAGE_SIZE) - 1u : 0;
        Mem = PhysicalMemoryAllocatePages(Pages + Extra);
        if (!Mem) {
            return 0;
        }
        Addr = (UINTN)Mem;
        Aligned = (Addr + (UINTN)Align - 1u) & ~((UINTN)Align - 1u);
        IwlZero((void *)Aligned, Size);
        gPageDb[Blk].Virt = (UINT8 *)Aligned;
        gPageDb[Blk].Phys = (UINT64)Aligned;
        gPageDb[Blk].Size = Size;
    }
    gPagingReady = 1;
    return 1;
}

static int IwlPagingFill(const IWL_FW_IMG *Img) {
    int SecIdx;
    UINT32 Blk;
    UINT32 Offset = 0;
    const IWL_FW_SEC *Css;
    const IWL_FW_SEC *Data;

    if (!Img || !gPagingReady) {
        return 0;
    }
    for (SecIdx = 0; SecIdx < Img->NumSec; SecIdx++) {
        if (Img->Sec[SecIdx].Offset == IWL_PAGING_SEP) {
            SecIdx++;
            break;
        }
    }
    if (SecIdx >= Img->NumSec - 1) {
        return 0;
    }
    Css = &Img->Sec[SecIdx];
    Data = &Img->Sec[SecIdx + 1];
    if (!Css->Data || !Data->Data || Css->Len == 0 || Data->Len == 0) {
        return 0;
    }
    if (Css->Len > gPageDb[0].Size) {
        return 0;
    }
    IwlCopy(gPageDb[0].Virt, Css->Data, Css->Len);
    IwlFlushDma(gPageDb[0].Virt, gPageDb[0].Size);

    for (Blk = 1; Blk < gNumPagingBlk + 1u; Blk++) {
        UINT32 Remaining = Data->Len - Offset;
        UINT32 Len = gPageDb[Blk].Size;

        if (Blk == gNumPagingBlk) {
            Len = Remaining;
            if (Remaining != gPagesInLast * IWL_FW_PAGING_SIZE) {
                return 0;
            }
        } else if (Len > Remaining) {
            return 0;
        }
        IwlCopy(gPageDb[Blk].Virt, Data->Data + Offset, Len);
        IwlFlushDma(gPageDb[Blk].Virt, gPageDb[Blk].Size);
        Offset += gPageDb[Blk].Size;
    }
    return 1;
}

static int IwlPagingSendOne(UINT32 Flags) {
    /*
     * 刀 #40：必须发完整 sizeof(iwl_fw_paging_cmd)=12+33*4。
     * 刀 #30 裁短长度 → FW 可能吞命令不回包（#39 o=4F n=00）。
     */
    UINT8 Cmd[12 + IWL_NUM_FW_PAGING_BLOCKS * 4];
    UINT32 Blk;

    IwlZero(Cmd, sizeof(Cmd));
    Flags |= (gPagesInLast << IWL_PAGING_CMD_LAST_PAGES_POS);
    Cmd[0] = (UINT8)Flags;
    Cmd[1] = (UINT8)(Flags >> 8);
    Cmd[2] = (UINT8)(Flags >> 16);
    Cmd[3] = (UINT8)(Flags >> 24);
    Cmd[4] = (UINT8)IWL_BLOCK_2_EXP_SIZE;
    Cmd[8] = (UINT8)gNumPagingBlk;
    Cmd[9] = (UINT8)(gNumPagingBlk >> 8);
    Cmd[10] = (UINT8)(gNumPagingBlk >> 16);
    Cmd[11] = (UINT8)(gNumPagingBlk >> 24);
    for (Blk = 0; Blk < gNumPagingBlk + 1u; Blk++) {
        UINT32 Addr = (UINT32)(gPageDb[Blk].Phys >> IWL_PAGE_2_EXP_SIZE);
        UINT32 Off = 12u + Blk * 4u;
        Cmd[Off] = (UINT8)Addr;
        Cmd[Off + 1] = (UINT8)(Addr >> 8);
        Cmd[Off + 2] = (UINT8)(Addr >> 16);
        Cmd[Off + 3] = (UINT8)(Addr >> 24);
    }
    IwlRxDrain();
    return IwlSendCmd(IWL_CMD_ID(IWL_FW_PAGING_BLOCK_CMD, IWL_LONG_GROUP, 0),
                      Cmd, (UINT32)sizeof(Cmd), 1) == 0;
}

static int IwlPagingSend(void) {
    /* 只发 SECURED|ENABLED 一次（刀#28 nosec 也 n=00，省超时） */
    if (IwlPagingSendOne(IWL_PAGING_CMD_IS_SECURED | IWL_PAGING_CMD_IS_ENABLED)) {
        return 1;
    }
    return 0;
}

int IwlPagingInit(const IWL_FW_IMG *Img) {
    if (!Img || Img->PagingMemSize == 0) {
        IwlLogVerb("page=skip");
        return 1;
    }
    if (!IwlPagingAlloc(Img->PagingMemSize)) {
        IwlLogStage("page=oom");
        return 0;
    }
    if (!IwlPagingFill(Img)) {
        IwlLogStage("page=fill");
        return 0;
    }
    if (!IwlPagingSend()) {
        IwlLogStage("page=fail");
        return 0;
    }
    IwlLogVerb("page=ok");
    return 1;
}
