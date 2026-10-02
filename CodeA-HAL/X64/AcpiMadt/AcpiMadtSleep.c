/*
 * AcpiMadtSleep.c — FACP/_S5_ 与 PM1 帮手（PR-S-acpimadt-1）
 */
#include "AcpiMadt.h"
#include "AcpiMadtPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "PCIe.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"
#include "Debug.h"

UINT64 GasIoAddress(const UINT8 *Gas) {
    /* ACPI GAS：Address @ +4（旧误用 +8）；SpaceId 1=SystemIO */
    if (Gas[0] != 1) {
        return 0;
    }
    return *(UINT64 *)(void *)(Gas + 4);
}

UINT64 GasAnyAddress(const UINT8 *Gas) {
    return *(UINT64 *)(void *)(Gas + 4);
}

void PowerStallMs(UINT32 Ms) {
    UINT32 Lo;
    UINT32 Hi;
    UINT64 T0;
    UINT64 Need;
    UINT64 Now;

    __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
    T0 = ((UINT64)Hi << 32) | Lo;
    /* NUC ~3GHz；偏大无妨，仅用于等 SCI_EN */
    Need = (UINT64)Ms * 3000000ULL;
    for (;;) {
        __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
        Now = ((UINT64)Hi << 32) | Lo;
        if (Now - T0 >= Need) {
            break;
        }
        HalCpuRelax();
    }
}

void PowerBootLine(const char *Text) {
    /* 只走 BootMark 一次：勿再 SmpLog，否则串口双打 */
    HalSerialBootMark(Text);
}

void PowerBootHex(const char *Prefix, UINT32 Value) {
#if !TOY_KERNEL_DEBUG
    (void)Prefix;
    (void)Value;
    return;
#else
    char Line[56];
    char Hex[12];
    int n = 0;
    int i;

    while (Prefix[n] != 0 && n < 36) {
        Line[n] = Prefix[n];
        n++;
    }
    HalSerialFormatHex(Hex, Value, 4);
    for (i = 0; Hex[i] != 0 && n < 52; i++) {
        Line[n++] = Hex[i];
    }
    Line[n++] = '\n';
    Line[n] = 0;
    PowerBootLine(Line);
#endif
}

static int ParseSlpTypFromAml(const UINT8 *Data, UINT32 Len, UINT8 *OutTyp) {
    UINT32 i;
    const UINT8 *P;
    UINT8 PkgLenByte;
    UINT8 Extra;

    if (Data == 0 || Len < 8 || OutTyp == 0) {
        return -1;
    }
    for (i = 0; i + 8 < Len; i++) {
        /* NameOp + "_S5_" */
        if (Data[i] != 0x08 || Data[i + 1] != '_' || Data[i + 2] != 'S' ||
            Data[i + 3] != '5' || Data[i + 4] != '_') {
            continue;
        }
        P = Data + i + 5;
        if (P >= Data + Len || *P != 0x12) { /* PackageOp */
            continue;
        }
        P++;
        if (P >= Data + Len) {
            continue;
        }
        PkgLenByte = *P;
        Extra = (UINT8)(PkgLenByte >> 6);
        if (Extra == 0) {
            P++;
        } else {
            if ((UINT32)(P - Data) + Extra + 1u >= Len) {
                continue;
            }
            P += Extra + 1;
        }
        if (P >= Data + Len) {
            continue;
        }
        P++; /* NumElements */
        if (P >= Data + Len) {
            continue;
        }
        if (*P == 0x0A && (P + 1) < Data + Len) { /* BytePrefix */
            *OutTyp = P[1] & 7u;
            return 0;
        }
        if (*P == 0x00) {
            *OutTyp = 0;
            return 0;
        }
        if (*P == 0x01) {
            *OutTyp = 1;
            return 0;
        }
        if (*P == 0x0B && (P + 2) < Data + Len) { /* WordPrefix */
            *OutTyp = P[1] & 7u;
            return 0;
        }
    }
    return -1;
}

void ParseSlpTypFromFacp(UINT64 RsdpPhys, ACPI_SDT_HEADER *Facp, UINT8 *P) {
    UINT32 Dsdt32;
    UINT64 Dsdt64;
    ACPI_SDT_HEADER *Dsdt;
    ACPI_RSDP *Rsdp;
    ACPI_SDT_HEADER *Root;
    UINT8 Typ;
    UINT32 i;
    UINT32 Entries;

    gSlpTypA = 0xFF;
    Dsdt32 = *(UINT32 *)(void *)(P + 40);
    Dsdt64 = 0;
    if (Facp->Length >= 148) {
        Dsdt64 = *(UINT64 *)(void *)(P + 140); /* X_DSDT */
    }
    Dsdt = 0;
    if (Dsdt64 != 0) {
        Dsdt = MapSdtHeader(Dsdt64);
    }
    if (Dsdt == 0 && Dsdt32 != 0) {
        Dsdt = MapSdtHeader((UINT64)Dsdt32);
    }
    Typ = 0xFF;
    if (Dsdt != 0 &&
        ParseSlpTypFromAml((const UINT8 *)(UINTN)Dsdt, Dsdt->Length, &Typ) == 0) {
        gSlpTypA = Typ;
        PowerBootHex("Boot: ACPI _S5_ Typ=", gSlpTypA);
        return;
    }

    /* 部分固件把 _S5_ 只放在 SSDT */
    if (RsdpPhys == 0 || MapPhys(RsdpPhys, sizeof(ACPI_RSDP)) != 0) {
        return;
    }
    Rsdp = (ACPI_RSDP *)(UINTN)RsdpPhys;
    Root = 0;
    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0) {
        Root = MapSdtHeader(Rsdp->XsdtAddress);
        if (Root) {
            Entries = (Root->Length - (UINT32)sizeof(ACPI_SDT_HEADER)) / 8u;
            if (Entries > ACPI_MAX_ROOT_ENTRIES) {
                Entries = ACPI_MAX_ROOT_ENTRIES;
            }
            for (i = 0; i < Entries; i++) {
                UINT64 Phys = ((UINT64 *)(void *)(Root + 1))[i];
                ACPI_SDT_HEADER *Tab = MapSdtHeader(Phys);
                if (Tab == 0 || !MemEq(Tab->Signature, "SSDT", 4)) {
                    continue;
                }
                if (ParseSlpTypFromAml((const UINT8 *)(UINTN)Tab, Tab->Length, &Typ) == 0) {
                    gSlpTypA = Typ;
                    PowerBootHex("Boot: ACPI _S5_ Typ=", gSlpTypA);
                    return;
                }
            }
        }
    }
}

void Pm1EnablePowerButton(UINT16 EvtPort, UINT16 EnPort) {
    UINT16 En;

    if (EvtPort == 0 || EnPort == 0) {
        return;
    }
    En = HalIoRead16(EnPort);
    HalIoWrite16(EnPort, (UINT16)(En | PM1_PWRBTN_EN));
    HalIoWrite16(EvtPort, PM1_PWRBTN_STS); /* W1C 清残留 */
}

void Pm1WriteSleep(UINT16 CntPort, UINT8 Typ) {
    UINT16 V;

    if (CntPort == 0) {
        return;
    }
    V = HalIoRead16(CntPort);
    V = (UINT16)((V & 0xC3FFu) | (((UINT16)Typ & 7u) << 10) | PM1_SLP_EN);
    HalIoWrite16(CntPort, V);
}
