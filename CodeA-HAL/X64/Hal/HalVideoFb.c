/*
 * HalVideoFb.c — PR-S3-halvideo-1：FB WC 映射与 PTE 诊断
 */
#include "HalVideo.h"
#include "Video.h"
#include "Hal.h"
#include "HalSerial.h"
#include "VirtualMemory.h"

/* 4K PTE：bit3 PWT、bit4 PCD、bit7 PAT；2M PDE：PAT 在 bit12，bit7=PS */
#define FB_PTE_PWT (1ULL << 3)
#define FB_PTE_PCD (1ULL << 4)
#define FB_PTE_PS  (1ULL << 7)
#define FB_PTE_PAT4K (1ULL << 7)
#define FB_PTE_PAT2M (1ULL << 12)

/*
 * 读 VA 叶项（4K PTE 或 2M PDE）。恒等窗是 2M，GOP 常在窗外另建 4K。
 * 不经 HalPageGetEntry：后者遇 huge 直接 0。
 */
static UINT64 FbLeafEntry(UINT64 Virt, int *OutHuge) {
    UINT64 Cr3;
    UINT64 *Pml4;
    UINT64 *Pdpt;
    UINT64 *Pd;
    UINT64 *Pt;
    UINT64 E;

    if (OutHuge) {
        *OutHuge = 0;
    }
    __asm__ volatile ("mov %%cr3, %0" : "=r"(Cr3));
    Pml4 = (UINT64 *)(UINTN)(Cr3 & ~0xFFFULL);
    E = Pml4[(Virt >> 39) & 0x1FF];
    if (!(E & HAL_PAGE_PRESENT)) {
        return 0;
    }
    Pdpt = (UINT64 *)(UINTN)(E & ~0xFFFULL);
    E = Pdpt[(Virt >> 30) & 0x1FF];
    if (!(E & HAL_PAGE_PRESENT)) {
        return 0;
    }
    Pd = (UINT64 *)(UINTN)(E & ~0xFFFULL);
    E = Pd[(Virt >> 21) & 0x1FF];
    if (!(E & HAL_PAGE_PRESENT)) {
        return 0;
    }
    if (E & FB_PTE_PS) {
        if (OutHuge) {
            *OutHuge = 1;
        }
        return E;
    }
    Pt = (UINT64 *)(UINTN)(E & ~0xFFFULL);
    return Pt[(Virt >> 12) & 0x1FF];
}

/*
 * PAT 索引 PAT:PCD:PWT。PR-G-fb-wc 后 PA1=WC（仅 PWT）；
 * PA0=WB；PA3=UC（PWT|PCD，xHCI PTE_MMIO）。
 */
static const char *FbCacheName(int Pat, int Pcd, int Pwt) {
    int Idx = ((Pat & 1) << 2) | ((Pcd & 1) << 1) | (Pwt & 1);

    switch (Idx) {
    case 0:
    case 4:
        return "WB";
    case 1:
        return "WC"; /* HalPatApplyWc：PA1 */
    case 5:
        return "WT";
    case 2:
    case 6:
        return "UC-";
    case 3:
    case 7:
        return "UC";
    default:
        return "?";
    }
}

static int gFbWcMapped;

UINT64 HalVideoFbMapFlags(void) {
    if (gFbWcMapped) {
        return HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE | HAL_PAGE_PWT;
    }
    return HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE;
}

void HalVideoEnableFbWc(void) {
    UINT64 Base;
    UINT64 Size;
    UINT64 Flags;

    Base = VideoFrameBufferBase();
    Size = VideoFrameBufferSize();
    if (Base == 0 || Size == 0) {
        HalSerialBootMark("Boot: FB-WC Skip (No FB)\n");
        return;
    }

    HalPatApplyWc();
    Flags = HAL_PAGE_PRESENT | HAL_PAGE_WRITABLE | HAL_PAGE_PWT;
    if (VirtualMemoryMapRange(Base, Base, (UINTN)Size, Flags) != 0) {
        HalSerialBootMark("Boot: FB-WC Map Fail (Keep WB)\n");
        return;
    }
    gFbWcMapped = 1;
    if (!HalCpuIsHypervisor()) {
        HalSerialBootMark("Boot: FB-WC OK (PAT PA1, LFB PWT)\n");
    }
}

/*
 * 填一行无 '\\n'：Boot: FB-PTE phys=… PWT= PCD= PAT= [2M] cache=…
 * boot 日志用；避免只靠 ring 尾（USB 日志易挤掉）。
 */
int HalVideoFbPteLine(char *Buf, UINTN Max) {
    UINT64 Phys;
    UINT64 Leaf;
    int Huge = 0;
    int Pwt;
    int Pcd;
    int Pat;
    UINTN N = 0;
    const char *P;
    const char *Cache;
    char Hex[20];

    if (!Buf || Max < 24) {
        return 0;
    }
    Buf[0] = 0;
    Phys = VideoFrameBufferBase();
    if (Phys == 0) {
        return 0;
    }
    Leaf = FbLeafEntry(Phys, &Huge);
    P = "Boot: FB-PTE Phys=";
    while (*P && N + 1 < Max) {
        Buf[N++] = *P++;
    }
    HalSerialFormatHex(Hex, Phys, 16);
    P = Hex;
    while (*P && N + 1 < Max) {
        Buf[N++] = *P++;
    }
    if (Leaf == 0 || !(Leaf & HAL_PAGE_PRESENT)) {
        P = " unmapped";
        while (*P && N + 1 < Max) {
            Buf[N++] = *P++;
        }
        Buf[N] = 0;
        return (int)N;
    }
    Pwt = (Leaf & FB_PTE_PWT) ? 1 : 0;
    Pcd = (Leaf & FB_PTE_PCD) ? 1 : 0;
    Pat = Huge ? ((Leaf & FB_PTE_PAT2M) ? 1 : 0) : ((Leaf & FB_PTE_PAT4K) ? 1 : 0);
    Cache = FbCacheName(Pat, Pcd, Pwt);
    P = " PWT=";
    while (*P && N + 1 < Max) {
        Buf[N++] = *P++;
    }
    if (N + 1 < Max) {
        Buf[N++] = (char)('0' + Pwt);
    }
    P = " PCD=";
    while (*P && N + 1 < Max) {
        Buf[N++] = *P++;
    }
    if (N + 1 < Max) {
        Buf[N++] = (char)('0' + Pcd);
    }
    P = " PAT=";
    while (*P && N + 1 < Max) {
        Buf[N++] = *P++;
    }
    if (N + 1 < Max) {
        Buf[N++] = (char)('0' + Pat);
    }
    if (Huge) {
        P = " 2M";
        while (*P && N + 1 < Max) {
            Buf[N++] = *P++;
        }
    }
    P = " cache=";
    while (*P && N + 1 < Max) {
        Buf[N++] = *P++;
    }
    while (*Cache && N + 1 < Max) {
        Buf[N++] = *Cache++;
    }
    Buf[N] = 0;
    return (int)N;
}

void HalVideoLogFbPte(void) {
    char Line[96];
    int N;

    N = HalVideoFbPteLine(Line, sizeof(Line) - 1);
    if (N <= 0) {
        return;
    }
    if (N < (int)sizeof(Line) - 1) {
        Line[N++] = '\n';
        Line[N] = 0;
    }
    if (!HalCpuIsHypervisor()) {
        HalSerialBootMark(Line);
    }
}
