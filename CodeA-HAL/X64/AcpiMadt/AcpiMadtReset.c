/*
 * AcpiMadtReset.c — 软关机与复位（PR-S-acpimadt-1）
 */
#include "AcpiMadt.h"
#include "AcpiMadtPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "PCIe.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"
#include "Debug.h"

void AcpiPowerOff(void) {
    UINT8 Order[4];
    UINT8 N;
    UINT8 i;
    UINT8 Typ;
    UINT8 Seen[8];

    HalIrqDisable();

    /* QEMU/Bochs 常见关机口 */
    HalIoWrite16(0x604, 0x2000);
    HalIoWrite16(0xB004, 0x2000);
    HalIoWrite16(0x4004, 0x3400);

    if (!gPowerReady || gPm1aCnt == 0) {
        return;
    }

    /*
     * 只试 _S5_ 与常见 5/7。禁止扫 Typ0～7：错误 SLP_TYP 会把芯片打进
     * S1/S3 类「假死」（屏亮/灯亮但无响应），开始菜单关机看起来像卡死。
     */
    N = 0;
    for (i = 0; i < 8; i++) {
        Seen[i] = 0;
    }
    if (gSlpTypA != 0xFF) {
        Order[N++] = gSlpTypA & 7u;
        Seen[gSlpTypA & 7u] = 1;
    }
    if (!Seen[5]) {
        Order[N++] = 5;
        Seen[5] = 1;
    }
    if (!Seen[7]) {
        Order[N++] = 7;
        Seen[7] = 1;
    }

    PowerBootLine("Boot: ACPI PowerOff\n");
    for (i = 0; i < N; i++) {
        Typ = Order[i];
        Pm1WriteSleep(gPm1aCnt, Typ);
        Pm1WriteSleep(gPm1bCnt, Typ);
        PowerStallMs(50);
    }
}

/* Linux/Windows：CF9 需先 |2 再写复位码，单写 0x06 部分机挂死不复位 */
static void Cf9Pulse(UINT8 Code) {
    UINT8 Cf9;

    Cf9 = (UINT8)(HalIoRead8(0xCF9) & (UINT8)~Code);
    HalIoWrite8(0xCF9, (UINT8)(Cf9 | 0x02));
    PowerStallMs(1);
    HalIoWrite8(0xCF9, (UINT8)(Cf9 | Code));
    PowerStallMs(15);
}

static void ResetWriteIo(UINT16 Port, UINT8 Access, UINT8 Value) {
    if (Access >= 3) {
        HalIoWrite32(Port, (UINT32)Value);
    } else if (Access == 2) {
        HalIoWrite16(Port, (UINT16)Value);
    } else {
        HalIoWrite8(Port, Value);
    }
}

static void ResetWritePci(UINT64 Addr, UINT8 Value) {
    UINT8 Dev = (UINT8)((Addr >> 32) & 0xFFu);
    UINT8 Fn = (UINT8)((Addr >> 16) & 0xFFu);
    UINT8 Off = (UINT8)(Addr & 0xFFu);
    UINT32 Aligned = (UINT32)(Off & ~3u);
    UINT32 Shift = (UINT32)(Off & 3u) * 8u;
    UINT32 Cur = PciReadConfig(0, Dev, Fn, (UINT8)Aligned);
    Cur = (Cur & ~(0xFFu << Shift)) | ((UINT32)Value << Shift);
    PciWriteConfig(0, Dev, Fn, (UINT8)Aligned, Cur);
}

void AcpiReset(void) {
    int Pass;

    if (gResetAddr == 0) {
        return;
    }
    PowerBootLine("Boot: ACPI Reset\n");
    for (Pass = 0; Pass < 2; Pass++) {
        if (gResetSpace == 1) {
            /* SystemIO：CF9 用双写脉冲（与 Linux BOOT_CF9 一致） */
            if (gResetAddr == 0xCF9ull) {
                Cf9Pulse(gResetValue);
            } else if (gResetAddr <= 0xFFFFull) {
                ResetWriteIo((UINT16)gResetAddr, gResetAccess, gResetValue);
                PowerStallMs(15);
            }
        } else if (gResetSpace == 0) {
            if (MapPhys(gResetAddr, 8) == 0) {
                if (gResetAccess >= 3) {
                    *(volatile UINT32 *)(UINTN)gResetAddr = (UINT32)gResetValue;
                } else if (gResetAccess == 2) {
                    *(volatile UINT16 *)(UINTN)gResetAddr = (UINT16)gResetValue;
                } else {
                    *(volatile UINT8 *)(UINTN)gResetAddr = gResetValue;
                }
            }
            PowerStallMs(15);
        } else if (gResetSpace == 2) {
            ResetWritePci(gResetAddr, gResetValue);
            PowerStallMs(15);
        }
    }
}

/* 供 HalCpuReboot：无 FADT 时也走 Linux CF9 脉冲 */
void AcpiCf9Reset(UINT8 Code) {
    if (Code == 0) {
        Code = 0x06;
    }
    PowerBootLine("Boot: CF9 Reset\n");
    Cf9Pulse(Code);
}

int AcpiPowerButtonPressed(void) {
    UINT16 Sts;

    if (!gPowerReady || gPm1aEvt == 0) {
        return 0;
    }

    /* 固件偶发清 EN：每次轮询重新武装 */
    if (gPm1aEn != 0) {
        UINT16 En = HalIoRead16(gPm1aEn);
        if ((En & PM1_PWRBTN_EN) == 0) {
            HalIoWrite16(gPm1aEn, (UINT16)(En | PM1_PWRBTN_EN));
        }
    }
    if (gPm1bEn != 0) {
        UINT16 En = HalIoRead16(gPm1bEn);
        if ((En & PM1_PWRBTN_EN) == 0) {
            HalIoWrite16(gPm1bEn, (UINT16)(En | PM1_PWRBTN_EN));
        }
    }

    Sts = HalIoRead16(gPm1aEvt);
    if (Sts & PM1_PWRBTN_STS) {
        HalIoWrite16(gPm1aEvt, PM1_PWRBTN_STS);
        return 1;
    }
    if (gPm1bEvt != 0) {
        Sts = HalIoRead16(gPm1bEvt);
        if (Sts & PM1_PWRBTN_STS) {
            HalIoWrite16(gPm1bEvt, PM1_PWRBTN_STS);
            return 1;
        }
    }
    return 0;
}
