/*
 * IwlProbe.c — PCI 8086:24fd + BAR0 Map（PR-N-wifi-2）
 */
#include "IwlPrivate.h"
#include "PCIe.h"
#include "VirtualMemory.h"
#include "HalSerial.h"

#ifndef PTE_PWT
#define PTE_PWT HAL_PAGE_PWT
#define PTE_PCD HAL_PAGE_PCD
#endif

int IwlPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut, UINT16 *DidOut) {
    int B;
    int D;
    int F;

    for (B = 0; B < 256; B++) {
        for (D = 0; D < 32; D++) {
            for (F = 0; F < 8; F++) {
                UINT32 VidDid = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT16 Vid = (UINT16)(VidDid & 0xFFFF);
                UINT16 Did = (UINT16)(VidDid >> 16);
                UINT32 Lo;
                UINT32 Hi;
                UINT64 Bar;
                UINT32 Cmd;

                if (Vid != IWL_VENDOR || Did != IWL_DID_8265) {
                    continue;
                }
                Cmd = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04);
                PciWriteConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04, Cmd | 0x06);

                Lo = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x10);
                if (Lo & 1u) {
                    continue;
                }
                Bar = Lo & 0xFFFFFFF0ULL;
                if (((Lo >> 1) & 3u) == 2u) {
                    Hi = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x14);
                    Bar |= ((UINT64)Hi) << 32;
                }
                if (Bar == 0) {
                    continue;
                }
                *Bus = (UINT8)B;
                *Dev = (UINT8)D;
                *Fn = (UINT8)F;
                if (BarOut) {
                    *BarOut = Bar;
                }
                if (DidOut) {
                    *DidOut = Did;
                }
                return 1;
            }
        }
    }
    return 0;
}

int IwlMapBar(UINT64 Bar) {
    UINT64 Sz;
    UINTN MapBytes;
    UINT32 Cmd;
    char Line[80];
    char Hex[12];
    int n = 0;

    if (Bar == 0) {
        return 0;
    }
    if (!VirtualMemoryEnabled()) {
        return 0;
    }
    /*
     * PciBarSize 会把 BAR 写成 0xFFFFFFFF；若 MSE 开着，设备解码瞬移，
     * 部分平台恢复后 FH 窗一直 A5A5。探测期间关 Memory Space。
     */
    Cmd = PciReadConfig(gIwlBus, gIwlDev, gIwlFn, 0x04);
    PciWriteConfig(gIwlBus, gIwlDev, gIwlFn, 0x04, Cmd & ~0x2u);
    Sz = PciBarSize(gIwlBus, gIwlDev, gIwlFn, 0);
    PciWriteConfig(gIwlBus, gIwlDev, gIwlFn, 0x04, Cmd | 0x06u);
    gIwlBarSize = Sz;
    MapBytes = IWL_BAR_MAP_BYTES;
    if (Sz > MapBytes) {
        MapBytes = (UINTN)Sz;
    }
    if (VirtualMemoryMapRange(Bar, Bar, MapBytes,
                              PTE_PRESENT | PTE_WRITABLE | PTE_PWT | PTE_PCD)
        != 0) {
        return 0;
    }
    gIwlBarPhys = Bar;
    gIwlBar = (volatile UINT8 *)(UINTN)Bar;
    gIwlBarOk = 1;
    Line[n++] = 'b'; Line[n++] = 'a'; Line[n++] = 'r';
    Line[n++] = 's'; Line[n++] = 'z'; Line[n++] = '=';
    HalSerialFormatHex(Hex, (UINT32)Sz, 8);
    Line[n++] = Hex[2]; Line[n++] = Hex[3]; Line[n++] = Hex[4]; Line[n++] = Hex[5];
    Line[n++] = Hex[6]; Line[n++] = Hex[7]; Line[n++] = Hex[8]; Line[n++] = Hex[9];
    Line[n++] = ' '; Line[n++] = 'p'; Line[n++] = '=';
    HalSerialFormatHex(Hex, (UINT32)Bar, 8);
    Line[n++] = Hex[2]; Line[n++] = Hex[3]; Line[n++] = Hex[4]; Line[n++] = Hex[5];
    Line[n++] = Hex[6]; Line[n++] = Hex[7]; Line[n++] = Hex[8]; Line[n++] = Hex[9];
    Line[n] = 0;
    IwlLogStage(Line);
    return 1;
}

static void IwlPciBmOn(UINT8 B, UINT8 D, UINT8 F) {
    UINT32 Cmd = PciReadConfig(B, D, F, 0x04);
    PciWriteConfig(B, D, F, 0x04, Cmd | 0x06u);
}

static void IwlPciAspmOffDev(UINT8 B, UINT8 D, UINT8 F) {
    int Cap = PciFindCap(B, D, F, 0x10);
    UINT32 Lnk;
    if (Cap <= 0) {
        return;
    }
    Lnk = PciReadConfig(B, D, F, (UINT8)(Cap + 0x10));
    PciWriteConfig(B, D, F, (UINT8)(Cap + 0x10), Lnk & ~0x3u);
}

/* 设备 + 覆盖到该 Bus 的 Type-1 桥：BM + 关 ASPM（FH Memory Read） */
void IwlPciPathPrep(void) {
    int B;
    int D;
    int F;

    IwlPciBmOn(gIwlBus, gIwlDev, gIwlFn);
    IwlPciAspmOffDev(gIwlBus, gIwlDev, gIwlFn);
    for (B = 0; B < 256; B++) {
        for (D = 0; D < 32; D++) {
            for (F = 0; F < 8; F++) {
                UINT32 Id = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT32 Ht;
                UINT32 Br;
                UINT8 Sec;
                UINT8 Sub;

                if ((Id & 0xFFFFu) == 0xFFFFu) {
                    continue;
                }
                Ht = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x0C);
                if (((Ht >> 16) & 0x7Fu) != 0x01u) {
                    continue;
                }
                Br = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x18);
                Sec = (UINT8)((Br >> 8) & 0xFFu);
                Sub = (UINT8)((Br >> 16) & 0xFFu);
                if (Sec <= gIwlBus && gIwlBus <= Sub) {
                    IwlPciBmOn((UINT8)B, (UINT8)D, (UINT8)F);
                    IwlPciAspmOffDev((UINT8)B, (UINT8)D, (UINT8)F);
                }
            }
        }
    }
}
