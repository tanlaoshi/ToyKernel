/*
 * IwlFh.c — FH 服务通道灌固件（PR-N-wifi-2）
 * 8000 secured 必须 FH DMA；scratch=PMM；桥 BM 见 IwlPciPathPrep。
 */
#include "IwlPrivate.h"
#include "PhysicalMemory.h"

#define IWL_WFPM_GP2           0xA030B4u
#define IWL_CSR_INT_COALESCING 0x004u
#define IWL_FH_TSSR_TX_STATUS  0x1EB0u

static UINT8 *gFhScratch;
static UINT64 gFhScratchPhys;
static UINT32 gFhFailDst;
static UINT32 gFhFailPhys;
static UINT32 gFhFailTssr;
static UINT32 gFhFailTcsr;
static int gFhFailWhy;
static UINT8 *gKwPage;
static UINT64 gKwPhys;

static void IwlCopy(void *D, const void *S, UINTN N) {
    UINT8 *d = (UINT8 *)D;
    const UINT8 *s = (const UINT8 *)S;
    UINTN i;
    for (i = 0; i < N; i++) {
        d[i] = s[i];
    }
}

static int IwlKwEnsure(void) {
    void *Mem;
    if (gKwPage) {
        return 1;
    }
    Mem = PhysicalMemoryAllocatePages(1);
    if (!Mem) {
        return 0;
    }
    gKwPage = (UINT8 *)Mem;
    gKwPhys = (UINT64)(UINTN)Mem;
    IwlFlushDma(gKwPage, PAGE_SIZE);
    return 1;
}

static int IwlScratchEnsure(void) {
    void *Mem;
    if (gFhScratch) {
        return 1;
    }
    Mem = PhysicalMemoryAllocatePages(1);
    if (!Mem) {
        gFhFailWhy = 3;
        return 0;
    }
    gFhScratch = (UINT8 *)Mem;
    gFhScratchPhys = (UINT64)(UINTN)Mem;
    return 1;
}

static int IwlWaitFhTx(UINT32 TimeoutMs) {
    UINT32 i;
    for (i = 0; i < TimeoutMs; i++) {
        UINT32 Int = IwlMmioR32(IWL_CSR_INT);
        UINT32 Fh = IwlMmioR32(IWL_CSR_FH_INT_STATUS);
        if ((Int & IWL_CSR_INT_FH_TX) || (Fh & IWL_CSR_FH_INT_TX_MASK)) {
            IwlMmioW32(IWL_CSR_FH_INT_STATUS, IWL_CSR_FH_INT_TX_MASK);
            IwlMmioW32(IWL_CSR_INT, IWL_CSR_INT_FH_TX);
            return 1;
        }
        IwlStallMs(1);
    }
    return 0;
}

static int IwlLoadChunk(UINT32 Dst, UINT64 Phys, UINT32 ByteCnt) {
    UINT32 Hi = (UINT32)((Phys >> 32) & 0xFu);
    UINT32 Cfg;

    gFhFailDst = Dst;
    gFhFailPhys = (UINT32)Phys;
    IwlFlushDma((const void *)(UINTN)Phys, ByteCnt);
    if (!IwlNicLock()) {
        gFhFailWhy = 1;
        return 0;
    }
    IwlMmioW32(IWL_CSR_FH_INT_STATUS, IWL_CSR_FH_INT_TX_MASK);
    IwlMmioW32(IWL_CSR_INT, IWL_CSR_INT_FH_TX);
    IwlMmioW32(IWL_FH_TCSR_CONFIG_9, IWL_FH_TCSR_PAUSE);
    IwlMmioW32(IWL_FH_SRVC_SRAM_ADDR, Dst);
    IwlMmioW32(IWL_FH_TFDIB_CTRL0_9, (UINT32)Phys);
    IwlMmioW32(IWL_FH_TFDIB_CTRL1_9, (Hi << IWL_FH_TFDIB_HI_SHIFT) | ByteCnt);
    IwlMmioW32(IWL_FH_TCSR_BUF_STS_9,
               (1u << IWL_FH_TCSR_TB_NUM_POS) | (1u << IWL_FH_TCSR_TB_IDX_POS)
               | IWL_FH_TCSR_TFDB_VALID);
    IwlMmioW32(IWL_FH_TCSR_CONFIG_9,
               IWL_FH_TCSR_ENABLE | IWL_FH_TCSR_CREDIT_DISABLE
               | IWL_FH_TCSR_CIRQ_ENDTFD);
    Cfg = IwlMmioR32(IWL_FH_TCSR_CONFIG_9);
    if (!IwlFhAliveVal(Cfg) || (Cfg & IWL_FH_TCSR_ENABLE) == 0) {
        gFhFailWhy = 2;
        gFhFailTcsr = Cfg;
        gFhFailTssr = IwlMmioR32(IWL_FH_TSSR_TX_STATUS);
        IwlNicUnlock();
        return 0;
    }
    IwlNicUnlock();
    if (!IwlWaitFhTx(2000)) {
        gFhFailWhy = 2;
        if (IwlNicLock()) {
            gFhFailTcsr = IwlMmioR32(IWL_FH_TCSR_CONFIG_9);
            gFhFailTssr = IwlMmioR32(IWL_FH_TSSR_TX_STATUS);
            IwlNicUnlock();
        } else {
            gFhFailTcsr = Cfg;
            gFhFailTssr = 0;
        }
        return 0;
    }
    return 1;
}

static int IwlLoadSection(const IWL_FW_SEC *Sec) {
    UINT32 Off = 0;
    if (!Sec || !Sec->Data || Sec->Len == 0) {
        return 1;
    }
    if (Sec->Offset == IWL_CPU1_CPU2_SEP || Sec->Offset == IWL_PAGING_SEP) {
        return 1;
    }
    if (!IwlScratchEnsure()) {
        return 0;
    }
    while (Off < Sec->Len) {
        UINT32 Dst = Sec->Offset + Off;
        UINT32 Chunk = Sec->Len - Off;
        int Ext = (Dst >= IWL_FW_MEM_EXT_START && Dst <= IWL_FW_MEM_EXT_END);
        if (Chunk > PAGE_SIZE) {
            Chunk = PAGE_SIZE;
        }
        IwlCopy(gFhScratch, Sec->Data + Off, Chunk);
        IwlFlushDma(gFhScratch, Chunk);
        if (Ext) {
            if (!IwlNicLock()) {
                gFhFailWhy = 1;
                return 0;
            }
            IwlPrphW(IWL_LMPM_CHICK,
                     IwlPrphR(IWL_LMPM_CHICK) | IWL_LMPM_CHICK_EXT_ADDR);
            IwlNicUnlock();
        }
        if (!IwlLoadChunk(Dst, gFhScratchPhys, Chunk)) {
            if (Ext && IwlNicLock()) {
                IwlPrphW(IWL_LMPM_CHICK,
                         IwlPrphR(IWL_LMPM_CHICK) & ~IWL_LMPM_CHICK_EXT_ADDR);
                IwlNicUnlock();
            }
            return 0;
        }
        if (Ext && IwlNicLock()) {
            IwlPrphW(IWL_LMPM_CHICK,
                     IwlPrphR(IWL_LMPM_CHICK) & ~IWL_LMPM_CHICK_EXT_ADDR);
            IwlNicUnlock();
        }
        Off += Chunk;
    }
    return 1;
}

static int IwlLoadCpuSections8000(const IWL_FW_IMG *Img, int Cpu, int *First) {
    int Shift = (Cpu == 1) ? 0 : 16;
    int SecNum = 0x1;
    int i;
    int Last = *First;
    if (Cpu != 1) {
        (*First)++;
    }
    for (i = *First; i < Img->NumSec; i++) {
        UINT32 Val;
        Last = i;
        if (!Img->Sec[i].Data
            || Img->Sec[i].Offset == IWL_CPU1_CPU2_SEP
            || Img->Sec[i].Offset == IWL_PAGING_SEP) {
            break;
        }
        if (!IwlLoadSection(&Img->Sec[i])) {
            return 0;
        }
        if (IwlNicLock()) {
            Val = IwlMmioR32(IWL_FH_UCODE_LOAD_STATUS);
            Val |= ((UINT32)SecNum << Shift);
            IwlMmioW32(IWL_FH_UCODE_LOAD_STATUS, Val);
            SecNum = (SecNum << 1) | 0x1;
            IwlNicUnlock();
        }
    }
    *First = Last;
    IwlMmioW32(IWL_CSR_INT_MASK, IWL_CSR_INT_FH_TX);
    if (IwlNicLock()) {
        IwlMmioW32(IWL_FH_UCODE_LOAD_STATUS, Cpu == 1 ? 0xFFFFu : 0xFFFFFFFFu);
        IwlNicUnlock();
    }
    return 1;
}

int IwlLoadUcode8000(const IWL_FW_IMG *Img) {
    int First = 0;
    gFhFailWhy = 0;
    gFhFailDst = 0;
    gFhFailPhys = 0;
    gFhFailTssr = 0;
    gFhFailTcsr = 0;
    if (!Img) {
        return 0;
    }
    IwlPciPathPrep();
    IwlMmioW32(IWL_CSR_INT_COALESCING, 0);
    IwlFhProbeAccess("fhpre");
    if (!IwlKwEnsure() || !IwlScratchEnsure()) {
        gFhFailWhy = 3;
        IwlFhLogFail(gFhFailWhy, gFhFailDst, gFhFailPhys, gFhFailTssr, gFhFailTcsr);
        return 0;
    }
    if (!IwlNicLock()) {
        gFhFailWhy = 1;
        IwlFhLogFail(gFhFailWhy, gFhFailDst, gFhFailPhys, gFhFailTssr, gFhFailTcsr);
        return 0;
    }
    /* 停 RX / 绑 KW / RELEASE：FH 窗全程持 MAC_ACCESS */
    IwlMmioW32(IWL_FH_RCSR_CHNL0_CONFIG, 0);
    IwlMmioW32(IWL_FH_KW_MEM_ADDR, (UINT32)(gKwPhys >> 4));
    IwlPrphW(IWL_WFPM_GP2, 0x01010101u);
    IwlPrphW(IWL_RELEASE_CPU_RESET, IWL_RELEASE_CPU_RESET_BIT);
    IwlNicUnlock();
    IwlFhProbeAccess("fhpost");
    if (!IwlLoadCpuSections8000(Img, 1, &First)) {
        IwlFhLogFail(gFhFailWhy, gFhFailDst, gFhFailPhys, gFhFailTssr, gFhFailTcsr);
        return 0;
    }
    if (!IwlLoadCpuSections8000(Img, 2, &First)) {
        IwlFhLogFail(gFhFailWhy, gFhFailDst, gFhFailPhys, gFhFailTssr, gFhFailTcsr);
        return 0;
    }
    return 1;
}
