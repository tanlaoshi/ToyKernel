/*
 * AlxProbe.c — PCI 查找、BAR、永久 MAC（PR-N-alx-1）
 */
#include "Alx.h"
#include "AlxPrivate.h"
#include "PCIe.h"
#include "VirtualMemory.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

volatile UINT8 *gAlxBar;
UINT64 gAlxBarPhys;
UINT16 gAlxDid;
UINT8 gAlxMac[6];
int gAlxReady;

static const UINT16 gAlxIds[] = {
    ALX_DID_AR8161,
    ALX_DID_AR8162,
    ALX_DID_AR8171,
    ALX_DID_AR8172,
    0
};

int AlxPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut,
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

                if (Vid != ALX_VENDOR) {
                    continue;
                }
                for (I = 0; gAlxIds[I] != 0; I++) {
                    if (Did == gAlxIds[I]) {
                        break;
                    }
                }
                if (gAlxIds[I] == 0) {
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
        return 0; /* 组播 / 本地管理位当无效永久址 */
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

/*
 * STAD 布局同 Linux alx_read_macaddr：
 * STAD0=6AF600DC STAD1=000B → 00:0B:6A:F6:00:DC
 */
int AlxReadMac(UINT8 Mac[6]) {
    UINT32 Mac0;
    UINT32 Mac1;

    if (!gAlxBar || !Mac) {
        return 0;
    }
    Mac0 = AlxMmioR32(ALX_STAD0);
    Mac1 = AlxMmioR32(ALX_STAD1);
    Mac[0] = (UINT8)((Mac1 >> 8) & 0xFFu);
    Mac[1] = (UINT8)(Mac1 & 0xFFu);
    Mac[2] = (UINT8)((Mac0 >> 24) & 0xFFu);
    Mac[3] = (UINT8)((Mac0 >> 16) & 0xFFu);
    Mac[4] = (UINT8)((Mac0 >> 8) & 0xFFu);
    Mac[5] = (UINT8)(Mac0 & 0xFFu);
    return MacValid(Mac);
}

static int WaitClear(UINT32 Reg, UINT32 Bit, int Spins) {
    while (Spins-- > 0) {
        if ((AlxMmioR32(Reg) & Bit) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

static int WaitReady(UINT32 Reg, UINT32 BusyMask, int Spins) {
    while (Spins-- > 0) {
        UINT32 V = AlxMmioR32(Reg);
        if ((V & BusyMask) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

int AlxLoadPermMac(UINT8 Mac[6]) {
    UINT32 Val;

    if (AlxReadMac(Mac)) {
        return 1;
    }

    /* eFuse：等空闲后触发 START */
    if (WaitReady(ALX_SLD, ALX_SLD_STAT | ALX_SLD_START, 100000)) {
        Val = AlxMmioR32(ALX_SLD);
        AlxMmioW32(ALX_SLD, Val | ALX_SLD_START);
        if (WaitClear(ALX_SLD, ALX_SLD_START, 100000) && AlxReadMac(Mac)) {
            return 1;
        }
    }

    /* flash / EEPROM（若存在） */
    Val = AlxMmioR32(ALX_EFLD);
    if (Val & (ALX_EFLD_F_EXIST | ALX_EFLD_E_EXIST)) {
        if (!WaitReady(ALX_EFLD, ALX_EFLD_STAT | ALX_EFLD_START, 100000)) {
            return 0;
        }
        AlxMmioW32(ALX_EFLD, Val | ALX_EFLD_START);
        if (WaitClear(ALX_EFLD, ALX_EFLD_START, 100000) && AlxReadMac(Mac)) {
            return 1;
        }
    }
    return 0;
}

static void LogMac(const UINT8 Mac[6]) {
    char Line[64];
    char Hex[12];
    int n = 0;
    const char *P = "Boot: alx MAC=";
    int i;

    while (*P && n < 20) {
        Line[n++] = *P++;
    }
    for (i = 0; i < 6; i++) {
        HalSerialFormatHex(Hex, Mac[i], 2);
        Line[n++] = Hex[0];
        Line[n++] = Hex[1];
        if (i < 5 && n < 62) {
            Line[n++] = ':';
        }
    }
    Line[n++] = '\n';
    Line[n] = 0;
    ToyLogDrv(Line);
}

int AlxSetup(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT64 Bar;
    UINT16 Did = 0;
    UINT8 Mac[6];

    if (gAlxReady) {
        return 1;
    }
    if (!VirtualMemoryEnabled()) {
        return 0;
    }
    if (!AlxPciFind(&Bus, &Dev, &Fn, &Bar, &Did)) {
        return 0;
    }
    if (VirtualMemoryMapRange(Bar, Bar, ALX_BAR_MAP_BYTES,
                              PTE_PRESENT | PTE_WRITABLE) != 0) {
        ToyLogDrv("Boot: alx map BAR fail\n");
        return 0;
    }
    gAlxBarPhys = Bar;
    gAlxBar = (volatile UINT8 *)(UINTN)Bar;
    gAlxDid = Did;

    if (!AlxLoadPermMac(Mac)) {
        ToyLogDrv("Boot: alx MAC unavailable\n");
        gAlxBar = 0;
        return 0;
    }
    gAlxMac[0] = Mac[0];
    gAlxMac[1] = Mac[1];
    gAlxMac[2] = Mac[2];
    gAlxMac[3] = Mac[3];
    gAlxMac[4] = Mac[4];
    gAlxMac[5] = Mac[5];
    gAlxReady = 1;
    LogMac(Mac);
    ToyLogDrv("Boot: alx probe ok did=");
    {
        char Hex[12];
        HalSerialFormatHex(Hex, Did, 4);
        ToyLogDrv(Hex);
        ToyLogDrv("\n");
    }
    return 1;
}

int AlxReady(void) {
    return gAlxReady;
}

void AlxGetMac(UINT8 Mac[6]) {
    int i;
    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gAlxReady ? gAlxMac[i] : 0;
    }
}

UINT16 AlxPciDid(void) {
    return gAlxDid;
}
