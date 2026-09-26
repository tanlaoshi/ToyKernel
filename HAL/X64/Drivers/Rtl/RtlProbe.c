/*
 * RtlProbe.c — PCI 查找、BAR、MAC（PR-N-rtl-1）
 */
#include "Rtl.h"
#include "RtlPrivate.h"
#include "PCIe.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

volatile UINT8 *gRtlBar;
UINT64 gRtlBarPhys;
UINT16 gRtlDid;
UINT8 gRtlMac[6];
int gRtlReady;

static const UINT16 gRtlIds[] = {
    RTL_DID_8168,
    RTL_DID_8161,
    RTL_DID_8162,
    RTL_DID_8167,
    RTL_DID_8169,
    RTL_DID_8136,
    0
};

int RtlPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut,
               UINT16 *DidOut) {
    int B;
    int D;
    int F;
    int I;

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

                if (Vid != RTL_VENDOR) {
                    continue;
                }
                for (I = 0; gRtlIds[I] != 0; I++) {
                    if (Did == gRtlIds[I]) {
                        break;
                    }
                }
                if (gRtlIds[I] == 0) {
                    continue;
                }

                Cmd = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04);
                PciWriteConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04, Cmd | 0x06);

                Lo = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x10);
                if (Lo & 1u) {
                    continue; /* 只要 MMIO BAR */
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
                *BarOut = Bar;
                if (DidOut) {
                    *DidOut = Did;
                }
                return 1;
            }
        }
    }
    return 0;
}

static int MacValid(const UINT8 Mac[6]) {
    int i;
    int Zero = 1;
    int Broadcast = 1;

    if (Mac[0] & 0x01u) {
        return 0;
    }
    for (i = 0; i < 6; i++) {
        if (Mac[i] != 0) {
            Zero = 0;
        }
        if (Mac[i] != 0xFFu) {
            Broadcast = 0;
        }
    }
    return !Zero && !Broadcast;
}

int RtlReadMac(UINT8 Mac[6]) {
    int i;

    if (!gRtlBar || !Mac) {
        return 0;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = RtlMmioR8(RTL_MAC0 + (UINT32)i);
    }
    return MacValid(Mac);
}

static void LogMac(const UINT8 Mac[6]) {
    char Line[64];
    char Hex[12];
    int n = 0;
    const char *P = "Boot: R8169 MAC=";
    int i;

    while (*P && n < 24) {
        Line[n++] = *P++;
    }
    for (i = 0; i < 6; i++) {
        HalSerialFormatHex(Hex, Mac[i], 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        if (i < 5 && n < 62) {
            Line[n++] = ':';
        }
    }
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogDrv(Line);
}

int RtlSetup(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT64 Bar;
    UINT16 Did = 0;
    UINT8 Mac[6];
    int i;

    if (gRtlReady) {
        return 1;
    }
    if (!VirtualMemoryEnabled()) {
        return 0;
    }
    if (!RtlPciFind(&Bus, &Dev, &Fn, &Bar, &Did)) {
        return 0;
    }
    if (VirtualMemoryMapRange(Bar, Bar, RTL_BAR_MAP_BYTES,
                              PTE_PRESENT | PTE_WRITABLE) != 0) {
        ToyLogDrv("Boot: R8169 Map BAR Fail\n");
        return 0;
    }
    gRtlBarPhys = Bar;
    gRtlBar = (volatile UINT8 *)(UINTN)Bar;
    gRtlDid = Did;

    if (!RtlReadMac(Mac)) {
        ToyLogDrv("Boot: R8169 MAC Unavailable\n");
        gRtlBar = 0;
        return 0;
    }
    for (i = 0; i < 6; i++) {
        gRtlMac[i] = Mac[i];
    }
    gRtlReady = 1;
    LogMac(Mac);
    ToyLogDrv("Boot: R8169 Probe OK Did=");
    {
        char Hex[12];
        HalSerialFormatHex(Hex, Did, 4);
        ToyLogDrv(Hex + 2);
        ToyLogDrv("\n");
    }
    return 1;
}

int RtlReady(void) {
    return gRtlReady;
}

void RtlGetMac(UINT8 Mac[6]) {
    int i;
    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gRtlReady ? gRtlMac[i] : 0;
    }
}

UINT16 RtlPciDid(void) {
    return gRtlDid;
}
