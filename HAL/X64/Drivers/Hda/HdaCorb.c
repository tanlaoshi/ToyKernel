/*
 * HdaCorb.c — PR-G-audio-2：控制器复位 + CORB/RIRB（poll，不写播放流）
 */
#include "Hda.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "ToySerialLog.h"
#include "Hal.h"

#ifndef PTE_PWT
#define PTE_PWT HAL_PAGE_PWT
#define PTE_PCD HAL_PAGE_PCD
#endif

#define HDA_REG_GCTL       0x08u
#define HDA_GCTL_CRST      0x1u
#define HDA_REG_WAKEEN     0x0Cu
#define HDA_REG_STATESTS   0x0Eu
#define HDA_REG_INTCTL     0x20u
#define HDA_REG_CORBLBASE  0x40u
#define HDA_REG_CORBUBASE  0x44u
#define HDA_REG_CORBWP     0x48u
#define HDA_REG_CORBRP     0x4Au
#define HDA_CORBRPRST      0x8000u
#define HDA_REG_CORBCTL    0x4Cu
#define HDA_CORCTL_RUN     0x02u
#define HDA_REG_CORBSIZE   0x4Eu
#define HDA_REG_RIRBLBASE  0x50u
#define HDA_REG_RIRBUBASE  0x54u
#define HDA_REG_RIRBWP     0x58u
#define HDA_RIRBWPRST      0x8000u
#define HDA_REG_RINTCNT    0x5Au
#define HDA_REG_RIRBCTL    0x5Cu
#define HDA_RCTL_DMAEN     0x02u
#define HDA_REG_RIRBSTS    0x5Du
#define HDA_REG_RIRBSIZE   0x5Eu

#define HDA_RING_TIMEOUT   5000u /* ×20us ≈ 100ms */

static int gHdaCorbOk;
static UINT32 *gCorb;
static UINT64 *gRirb;
static UINT16 gCorbEntries;
static UINT16 gLastRirbWp;

static void StallUs(UINT32 Us) {
    UINT32 Lo, Hi;
    UINT64 T0, Need, Now;
    if (Us == 0) {
        return;
    }
    __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
    T0 = ((UINT64)Hi << 32) | Lo;
    Need = (UINT64)Us * 3000ULL;
    do {
        __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
        Now = ((UINT64)Hi << 32) | Lo;
        __asm__ volatile ("pause");
    } while (Now - T0 < Need);
}

static void Zero(void *P, UINTN N) {
    UINT8 *B = (UINT8 *)P;
    while (N--) {
        *B++ = 0;
    }
}

int HdaCorbOk(void) {
    return gHdaCorbOk;
}

static int WaitGctl(UINT32 WantSet, UINT32 Timeout) {
    UINT32 I;

    for (I = 0; I < Timeout; I++) {
        UINT32 V = HdaMmioRead32(HDA_REG_GCTL);
        if (WantSet) {
            if (V & HDA_GCTL_CRST) {
                return 1;
            }
        } else if ((V & HDA_GCTL_CRST) == 0) {
            return 1;
        }
        StallUs(10);
    }
    return 0;
}

static int ControllerReset(void) {
    UINT32 I;
    UINT16 Sts;
    UINT16 Prev;

    HdaMmioWrite32(HDA_REG_INTCTL, 0);
    HdaMmioWrite32(HDA_REG_GCTL, HdaMmioRead32(HDA_REG_GCTL) & ~HDA_GCTL_CRST);
    if (!WaitGctl(0, 10000u)) {
        ToyLogBoot("Boot: hda reset enter fail\n");
        return 0;
    }
    HdaMmioWrite32(HDA_REG_GCTL, HdaMmioRead32(HDA_REG_GCTL) | HDA_GCTL_CRST);
    if (!WaitGctl(1, 10000u)) {
        ToyLogBoot("Boot: hda reset leave fail\n");
        return 0;
    }
    /* 开 wake，等模拟+HDMI 都进 STATESTS（勿写清） */
    HdaMmioWrite16(HDA_REG_WAKEEN, 0x7FFFu);
    Prev = 0;
    Sts = 0;
    for (I = 0; I < 100u; I++) {
        Sts = HdaMmioRead16(HDA_REG_STATESTS) & 0x7FFFu;
        if (Sts != 0 && Sts == Prev && I >= 5u) {
            break;
        }
        Prev = Sts;
        StallUs(100);
    }
    ToyLogBoot("Boot: hda statests=");
    ToyLogBootHex32((UINT32)Sts);
    ToyLogBoot("\n");
    return 1;
}

static UINT8 PickSizeCode(UINT32 SizeReg) {
    UINT8 Raw = HdaMmioRead8(SizeReg);
    UINT8 Cap = (UINT8)((Raw >> 4) & 3u);
    UINT8 Cur = (UINT8)(Raw & 3u);
    UINT8 Prefer;

    /*
     * 勿把已硬线 256（Cur==2 而 Cap 读成 0）降成 2 项——NUC 曾被降成 entries=2。
     */
    if (Cap == 2u || Cap == 3u || Cur == 2u) {
        Prefer = 2u;
    } else if (Cap == 1u || Cur == 1u) {
        Prefer = 1u;
    } else {
        Prefer = 2u; /* 强试 256 */
    }
    HdaMmioWrite8(SizeReg, Prefer);
    Raw = HdaMmioRead8(SizeReg);
    Cur = (UINT8)(Raw & 3u);
    if (Cur == Prefer || Cur == 2u || Cur == 1u) {
        return Cur == 0u ? Prefer : Cur;
    }
    return Prefer;
}

static UINT16 SizeCodeToEntries(UINT8 Code) {
    if (Code == 2u) {
        return 256u;
    }
    if (Code == 1u) {
        return 16u;
    }
    return 2u;
}

static int ResetCorbRp(void) {
    UINT32 I;

    HdaMmioWrite16(HDA_REG_CORBRP, HDA_CORBRPRST);
    for (I = 0; I < 1000u; I++) {
        if (HdaMmioRead16(HDA_REG_CORBRP) & HDA_CORBRPRST) {
            break;
        }
        StallUs(10);
    }
    HdaMmioWrite16(HDA_REG_CORBRP, 0);
    for (I = 0; I < 1000u; I++) {
        if ((HdaMmioRead16(HDA_REG_CORBRP) & HDA_CORBRPRST) == 0) {
            return 1;
        }
        StallUs(10);
    }
    /* 部分控制器不吐位：仍写 WP=0 继续 */
    return 1;
}

int HdaCorbInit(void) {
    UINT8 *Dma;
    UINT64 Phys;
    UINT16 Entries;

    if (gHdaCorbOk) {
        return 1;
    }
    if (!HdaMmioOk()) {
        return 0;
    }
    if (!ControllerReset()) {
        return 0;
    }

    HdaMmioWrite8(HDA_REG_CORBCTL, 0);
    HdaMmioWrite8(HDA_REG_RIRBCTL, 0);
    {
        UINT8 Code = PickSizeCode(HDA_REG_CORBSIZE);
        UINT8 Rcode = PickSizeCode(HDA_REG_RIRBSIZE);
        if (Rcode < Code) {
            Rcode = Code;
            HdaMmioWrite8(HDA_REG_RIRBSIZE, Rcode);
        }
        Entries = SizeCodeToEntries(Code);
        gCorbEntries = Entries;
    }
    if (Entries < 2u) {
        ToyLogBoot("Boot: hda corb size bad\n");
        return 0;
    }

    Dma = (UINT8 *)PhysicalMemoryAllocatePages(1);
    if (!Dma) {
        ToyLogBoot("Boot: hda corb dma fail\n");
        return 0;
    }
    Phys = (UINT64)(UINTN)Dma;
    if (Phys > 0xFFFFFFFFu) {
        ToyLogBoot("Boot: hda corb dma >4G\n");
        return 0;
    }
    Zero(Dma, PAGE_SIZE);
    if (VirtualMemoryMapRange(Phys, Phys, PAGE_SIZE,
                              PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD) != 0) {
        ToyLogBoot("Boot: hda corb map fail\n");
        return 0;
    }

    gCorb = (UINT32 *)(UINTN)Dma;
    gRirb = (UINT64 *)(UINTN)(Dma + 1024u);

    HdaMmioWrite32(HDA_REG_CORBLBASE, (UINT32)Phys);
    HdaMmioWrite32(HDA_REG_CORBUBASE, 0);
    HdaMmioWrite32(HDA_REG_RIRBLBASE, (UINT32)Phys + 1024u);
    HdaMmioWrite32(HDA_REG_RIRBUBASE, 0);

    if (!ResetCorbRp()) {
        ToyLogBoot("Boot: hda corb rp fail\n");
        return 0;
    }
    HdaMmioWrite16(HDA_REG_CORBWP, 0);
    HdaMmioWrite16(HDA_REG_RIRBWP, HDA_RIRBWPRST);
    StallUs(20);
    HdaMmioWrite16(HDA_REG_RIRBWP, 0);
    HdaMmioWrite16(HDA_REG_RINTCNT, 1);
    HdaMmioWrite8(HDA_REG_RIRBSTS, 0x5u);
    gLastRirbWp = 0;

    HdaMmioWrite8(HDA_REG_RIRBCTL, HDA_RCTL_DMAEN);
    HdaMmioWrite8(HDA_REG_CORBCTL, HDA_CORCTL_RUN);

    gHdaCorbOk = 1;
    ToyLogBoot("Boot: hda corb ok entries=");
    ToyLogBootHex32((UINT32)gCorbEntries);
    ToyLogBoot("\n");
    return 1;
}

int HdaCorbVerb(UINT8 Cad, UINT8 Nid, UINT32 VerbPayload, UINT32 *RespOut) {
    UINT16 Wp;
    UINT16 Next;
    UINT16 Rp;
    UINT32 I;
    UINT32 Verb;
    UINT64 Ent;

    if (!gHdaCorbOk || !gCorb || !gRirb || gCorbEntries < 2u) {
        return 0;
    }
    Verb = ((UINT32)Cad << 28) | ((UINT32)Nid << 20) | (VerbPayload & 0xFFFFFu);
    Wp = HdaMmioRead16(HDA_REG_CORBWP) & 0xFFu;
    Next = (UINT16)((Wp + 1u) % gCorbEntries);
    Rp = HdaMmioRead16(HDA_REG_CORBRP) & 0xFFu;
    if (Next == Rp) {
        ToyLogBoot("Boot: hda corb full\n");
        return 0;
    }
    gCorb[Next] = Verb;
    __asm__ volatile ("mfence" ::: "memory");
    HdaMmioWrite16(HDA_REG_CORBWP, Next);

    for (I = 0; I < HDA_RING_TIMEOUT; I++) {
        UINT16 Rwp = HdaMmioRead16(HDA_REG_RIRBWP) & 0xFFu;
        if (Rwp != gLastRirbWp) {
            gLastRirbWp = Rwp;
            Ent = gRirb[Rwp];
            if (RespOut) {
                *RespOut = (UINT32)Ent;
            }
            return 1;
        }
        StallUs(20);
    }
    ToyLogBoot("Boot: hda verb timeout\n");
    return 0;
}
