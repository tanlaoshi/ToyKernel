/*
 * AcpiMadtPower.c — FACP 电源初始化（PR-S-acpimadt-1）
 */
#include "AcpiMadt.h"
#include "AcpiMadtPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "PCIe.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"
#include "Debug.h"

UINT16 gPm1aEvt;
UINT16 gPm1aCnt;
UINT16 gPm1bEvt;
UINT16 gPm1bCnt;
UINT16 gPm1aEn;
UINT16 gPm1bEn;
UINT8  gPm1EvtLen;
UINT8  gSlpTypA; /* 来自 _S5_；0xFF=未知 */
UINT8  gPowerReady;
/* FADT RESET_REG：0=mem 1=io 2=pci；Addr=0 表示无 */
UINT8  gResetSpace;
UINT64 gResetAddr;
UINT8  gResetValue;
UINT8  gResetAccess; /* GAS AccessSize：1=byte 2=word 3=dword */

int AcpiPowerInit(UINT64 RsdpPhys) {
    ACPI_SDT_HEADER *Facp;
    UINT8 *P;
    UINT32 Pm1aEvt;
    UINT32 Pm1aCnt;
    UINT32 Pm1bEvt;
    UINT32 Pm1bCnt;
    UINT32 SmiCmd;
    UINT32 Flags;
    UINT32 LegEvt;
    UINT32 LegCnt;
    UINT32 LegEvtB;
    UINT32 LegCntB;
    UINT8 AcpiEnable;
    UINT16 Cnt;
    UINT32 Wait;
    UINT64 A;

    gPowerReady = 0;
    gPm1aEvt = 0;
    gPm1aCnt = 0;
    gPm1bEvt = 0;
    gPm1bCnt = 0;
    gPm1aEn = 0;
    gPm1bEn = 0;
    gPm1EvtLen = 4;
    gSlpTypA = 0xFF;
    gResetSpace = 0;
    gResetAddr = 0;
    gResetValue = 0;
    gResetAccess = 1;
    Facp = FindFacp(RsdpPhys);
    if (!Facp || Facp->Length < 116) {
        PowerBootLine("Boot: ACPI No FACP\n");
        return -1;
    }
    if (MapPhys((UINT64)(UINTN)Facp, Facp->Length) != 0) {
        PowerBootLine("Boot: ACPI FACP Map Fail\n");
        return -1;
    }
    P = (UINT8 *)Facp;
    /* ACPI 1.0 FADT：PM1a_EVT@56 PM1b@60 PM1a_CNT@64 PM1b_CNT@68 PM1_EVT_LEN@88 */
    Pm1aEvt = *(UINT32 *)(void *)(P + 56);
    Pm1bEvt = *(UINT32 *)(void *)(P + 60);
    Pm1aCnt = *(UINT32 *)(void *)(P + 64);
    Pm1bCnt = *(UINT32 *)(void *)(P + 68);
    gPm1EvtLen = P[88];
    if (gPm1EvtLen == 0) {
        gPm1EvtLen = 4;
    }
    SmiCmd = *(UINT32 *)(void *)(P + 48);
    AcpiEnable = P[52];
    Flags = *(UINT32 *)(void *)(P + 112);

    /*
     * ACPI 2.0+：优先 X_GAS（规范/Linux）；保留 legacy 以便 X 口无效时回退。
     * 旧逻辑「仅 32 位为 0 才读 X」在 NUC 上会用到废弃口 → 短按无 STS。
     */
    LegEvt = Pm1aEvt;
    LegCnt = Pm1aCnt;
    LegEvtB = Pm1bEvt;
    LegCntB = Pm1bCnt;
    if (Facp->Length >= 184) {
        A = GasIoAddress(P + 148); /* X_PM1a_EVT */
        if (A != 0 && A <= 0xFFFFu) {
            Pm1aEvt = (UINT32)A;
        }
        A = GasIoAddress(P + 160); /* X_PM1b_EVT */
        if (A != 0 && A <= 0xFFFFu) {
            Pm1bEvt = (UINT32)A;
        }
        A = GasIoAddress(P + 172); /* X_PM1a_CNT */
        if (A != 0 && A <= 0xFFFFu) {
            Pm1aCnt = (UINT32)A;
        }
        A = GasIoAddress(P + 184); /* X_PM1b_CNT */
        if (A != 0 && A <= 0xFFFFu) {
            Pm1bCnt = (UINT32)A;
        }
    }
    if (Pm1aCnt == 0 || Pm1aCnt > 0xFFFFu || Pm1aEvt == 0 || Pm1aEvt > 0xFFFFu) {
        /* X 无效则退回 legacy */
        Pm1aEvt = LegEvt;
        Pm1aCnt = LegCnt;
        Pm1bEvt = LegEvtB;
        Pm1bCnt = LegCntB;
    }
    if (Pm1aCnt == 0 || Pm1aCnt > 0xFFFFu || Pm1aEvt == 0 || Pm1aEvt > 0xFFFFu) {
        PowerBootLine("Boot: ACPI Power Ports Missing\n");
        return -1;
    }
    /* 选出的口若全 1（未解码），改试 legacy */
    if (HalIoRead16((UINT16)Pm1aCnt) == 0xFFFFu && LegCnt != 0 && LegCnt <= 0xFFFFu &&
        LegCnt != Pm1aCnt) {
        PowerBootLine("Boot: ACPI X_GAS Dead, Use Legacy\n");
        Pm1aEvt = LegEvt;
        Pm1aCnt = LegCnt;
        Pm1bEvt = LegEvtB;
        Pm1bCnt = LegCntB;
    }
    gPm1aEvt = (UINT16)Pm1aEvt;
    gPm1aCnt = (UINT16)Pm1aCnt;
    gPm1aEn = (UINT16)(gPm1aEvt + (gPm1EvtLen / 2));
    if (Pm1bEvt != 0 && Pm1bEvt <= 0xFFFFu) {
        gPm1bEvt = (UINT16)Pm1bEvt;
        gPm1bEn = (UINT16)(gPm1bEvt + (gPm1EvtLen / 2));
    }
    if (Pm1bCnt != 0 && Pm1bCnt <= 0xFFFFu) {
        gPm1bCnt = (UINT16)Pm1bCnt;
    }

    /*
     * 无 SCI_EN 时多数板卡不锁存 PWRBTN_STS；写 SMI_CMD(ACPI_ENABLE) 切入 ACPI 模式。
     * 真机 SMM 可能较慢，毫秒级等待（勿只 pause 空转）。
     */
    Cnt = HalIoRead16(gPm1aCnt);
    if ((Cnt & PM1_SCI_EN) == 0 && SmiCmd != 0 && SmiCmd <= 0xFFFFu && AcpiEnable != 0) {
        HalIoWrite8((UINT16)SmiCmd, AcpiEnable);
        for (Wait = 0; Wait < 50u; Wait++) {
            if (HalIoRead16(gPm1aCnt) & PM1_SCI_EN) {
                break;
            }
            PowerStallMs(1);
        }
    }
    Cnt = HalIoRead16(gPm1aCnt);
    if ((Cnt & PM1_SCI_EN) == 0) {
        /* 部分 PCH 允许直接置位；写了无效也无害 */
        HalIoWrite16(gPm1aCnt, (UINT16)(Cnt | PM1_SCI_EN));
        PowerStallMs(1);
    }

    Pm1EnablePowerButton(gPm1aEvt, gPm1aEn);
    Pm1EnablePowerButton(gPm1bEvt, gPm1bEn);
    ParseSlpTypFromFacp(RsdpPhys, Facp, P);

    /* ACPI 2.0+ RESET_REG（真机重启优先；无 flag 但口为 0xCF9 也收） */
    if (Facp->Length >= 129) {
        UINT8 Space = P[116];
        UINT64 Ra = GasAnyAddress(P + 116);
        UINT8 Val = P[128];
        int Want = 0;

        if (Flags & FADT_FLAG_RESET_REG) {
            Want = 1;
        } else if (Space == 1 && Ra == 0xCF9ull) {
            /* 部分固件漏 RESET_REG_SUP，但 GAS 已填 CF9 */
            Want = 1;
            PowerBootLine("Boot: ACPI Reset=CF9 (No Flag)\n");
        }
        if (Want && (Space <= 2) && Ra != 0) {
            gResetSpace = Space;
            gResetAddr = Ra;
            gResetValue = Val ? Val : 0x06;
            gResetAccess = P[119];
            if (gResetAccess == 0) {
                gResetAccess = 1;
            }
            PowerBootHex("Boot: ACPI Reset Space=", Space);
            PowerBootHex("Boot: ACPI Reset Addr=", (UINT32)Ra);
            PowerBootHex("Boot: ACPI Reset Val=", gResetValue);
        }
    }

    PowerBootHex("Boot: ACPI PM1 Evt=", gPm1aEvt);
    PowerBootHex("Boot: ACPI PM1 Cnt=", gPm1aCnt);
    PowerBootHex("Boot: ACPI PM1 En=", gPm1aEn);
    PowerBootHex("Boot: ACPI SCI=", HalIoRead16(gPm1aCnt) & PM1_SCI_EN);
    PowerBootHex("Boot: ACPI EnRd=", HalIoRead16(gPm1aEn) & PM1_PWRBTN_EN);
    if (Flags & FADT_FLAG_PWR_BUTTON) {
        PowerBootLine("Boot: ACPI PwrBtn=AML (Still Arm Fixed)\n");
    } else {
#if TOY_KERNEL_DEBUG
        PowerBootLine("Boot: ACPI PwrBtn=Fixed\n");
#endif
    }

    gPowerReady = 1;
    return 0;
}
