/*
 * E1000Probe.c — PCI 查找、NVM MAC、MSI（PR-S-e1000-1）
 */
#include "E1000.h"
#include "E1000Private.h"
#include "PCIe.h"
#include "PhysicalMemory.h"
#include "VirtualMemory.h"
#include "Debug.h"
#include "Hal.h"
#include "HalPort.h"
#include "HalSerial.h"
#include "Net.h"
#include "ToySerialLog.h"

static const UINT16 gE1000Ids[] = {
    E1000_DID_82540EM, /* 82540EM — QEMU e1000 */
    0x100F,            /* 82545EM */
    E1000_DID_82574L,  /* 82574L — e1000e */
    0x10F5,            /* 82567LM */
    E1000_DID_I219_LM, /* I219-LM — NUC 现场 8086:156F */
    0
};

int PciFindE1000(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut,
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

                if (Vid != E1000_VENDOR) {
                    continue;
                }
                for (I = 0; gE1000Ids[I] != 0; I++) {
                    if (Did == gE1000Ids[I]) {
                        break;
                    }
                }
                if (gE1000Ids[I] == 0) {
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

/* 82571+/82574 EERD：START + ADDR<<2，DONE=bit1，DATA=bits31:16 */
static int EepromReadWordE1000e(UINT16 Addr, UINT16 *Out) {
    UINT32 Val;
    int Spin;

    MmioW32(E1000_REG_EERD,
            E1000_EERD_START | ((UINT32)Addr << E1000_EERD_ADDR_SHIFT));
    Spin = 100000;
    while (Spin-- > 0) {
        Val = MmioR32(E1000_REG_EERD);
        if (Val & E1000_EERD_DONE) {
            *Out = (UINT16)(Val >> E1000_EERD_DATA_SHIFT);
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

/* 82540 旧 EERD：ADDR<<8，DONE=bit4 */
static int EepromReadWordLegacy(UINT16 Addr, UINT16 *Out) {
    UINT32 Val;
    int Spin;

    MmioW32(E1000_REG_EERD,
            E1000_EERD_START | ((UINT32)Addr << E1000_EERD_ADDR_LEGACY_SHIFT));
    Spin = 100000;
    while (Spin-- > 0) {
        Val = MmioR32(E1000_REG_EERD);
        if (Val & E1000_EERD_DONE_LEGACY) {
            *Out = (UINT16)(Val >> E1000_EERD_DATA_SHIFT);
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

static int EepromReadWord(UINT16 Addr, UINT16 *Out) {
    /* I219 与 82574 同走 EERD 新布局试读；真 flash 路径留给 PR-N-i219-mac */
    if (gPciDid == E1000_DID_82574L || gPciDid == 0x10F5 ||
        gPciDid == E1000_DID_I219_LM) {
        if (EepromReadWordE1000e(Addr, Out)) {
            return 1;
        }
    }
    if (EepromReadWordLegacy(Addr, Out)) {
        return 1;
    }
    if (gPciDid != E1000_DID_82574L && gPciDid != 0x10F5 &&
        gPciDid != E1000_DID_I219_LM) {
        return EepromReadWordE1000e(Addr, Out);
    }
    return 0;
}

static int MacFromEeprom(void) {
    UINT16 W0;
    UINT16 W1;
    UINT16 W2;

    if (!EepromReadWord(0, &W0) || !EepromReadWord(1, &W1) ||
        !EepromReadWord(2, &W2)) {
        return 0;
    }
    if ((W0 | W1 | W2) == 0 || (W0 == 0xFFFF && W1 == 0xFFFF && W2 == 0xFFFF)) {
        return 0;
    }
    gE1000Mac[0] = (UINT8)(W0 & 0xFF);
    gE1000Mac[1] = (UINT8)(W0 >> 8);
    gE1000Mac[2] = (UINT8)(W1 & 0xFF);
    gE1000Mac[3] = (UINT8)(W1 >> 8);
    gE1000Mac[4] = (UINT8)(W2 & 0xFF);
    gE1000Mac[5] = (UINT8)(W2 >> 8);
    MmioW32(E1000_REG_RAL,
            (UINT32)gE1000Mac[0] | ((UINT32)gE1000Mac[1] << 8) |
            ((UINT32)gE1000Mac[2] << 16) | ((UINT32)gE1000Mac[3] << 24));
    MmioW32(E1000_REG_RAH,
            (UINT32)gE1000Mac[4] | ((UINT32)gE1000Mac[5] << 8) | (1u << 31));
    return 1;
}

static void ProgramClassroomMac(void) {
    gE1000Mac[0] = 0x52;
    gE1000Mac[1] = 0x54;
    gE1000Mac[2] = 0x00;
    gE1000Mac[3] = 0x12;
    gE1000Mac[4] = 0x34;
    gE1000Mac[5] = 0x56;
    MmioW32(E1000_REG_RAL,
            (UINT32)gE1000Mac[0] | ((UINT32)gE1000Mac[1] << 8) |
            ((UINT32)gE1000Mac[2] << 16) | ((UINT32)gE1000Mac[3] << 24));
    MmioW32(E1000_REG_RAH,
            (UINT32)gE1000Mac[4] | ((UINT32)gE1000Mac[5] << 8) | (1u << 31));
}

void ReadMac(void) {
    UINT32 Ral = MmioR32(E1000_REG_RAL);
    UINT32 Rah = MmioR32(E1000_REG_RAH);

    gE1000Mac[0] = (UINT8)(Ral & 0xFF);
    gE1000Mac[1] = (UINT8)((Ral >> 8) & 0xFF);
    gE1000Mac[2] = (UINT8)((Ral >> 16) & 0xFF);
    gE1000Mac[3] = (UINT8)((Ral >> 24) & 0xFF);
    gE1000Mac[4] = (UINT8)(Rah & 0xFF);
    gE1000Mac[5] = (UINT8)((Rah >> 8) & 0xFF);
    if ((Ral | (Rah & 0xFFFFu)) != 0) {
        return;
    }
    /* RAL 空：试 NVM（82574 常见），再课堂假 MAC */
    (void)MmioR32(E1000_REG_EEC);
    if (MacFromEeprom()) {
        DebugWrite("e1000: mac from nvm\n");
        return;
    }
    ProgramClassroomMac();
    DebugWrite("e1000: mac classroom fallback\n");
}

/* PR-H4e-3：PCI MSI → VEC_E1000；失败则保持 poll（IMC 全掩） */
static void FillPciBars(USB_CONTROLLER *Dev) {
    int i;

    for (i = 0; i < 6;) {
        UINT32 Lo = PciReadConfig(Dev->Bus, Dev->Device, Dev->Function,
                                  (UINT8)(0x10 + i * 4));
        UINT64 Bar;
        UINT32 Type;

        if (Lo & 1u) {
            Dev->Bar[i] = Lo & ~0x3u;
            i++;
            continue;
        }
        Bar = Lo & 0xFFFFFFF0ULL;
        Type = (Lo >> 1) & 3u;
        if (Type == 2u && i + 1 < 6) {
            UINT32 Hi = PciReadConfig(Dev->Bus, Dev->Device, Dev->Function,
                                      (UINT8)(0x10 + (i + 1) * 4));
            Bar |= ((UINT64)Hi) << 32;
            Dev->Bar[i] = Bar;
            Dev->Bar[i + 1] = 0;
            i += 2;
        } else {
            Dev->Bar[i] = Bar;
            i++;
        }
    }
}

int TryEnableMsiRx(void) {
    USB_CONTROLLER Dev;

    ZeroMemory(&Dev, sizeof(Dev));
    Dev.Bus = gPciBus;
    Dev.Device = gPciDev;
    Dev.Function = gPciFn;
    FillPciBars(&Dev);
    if (Dev.Bar[0] == 0) {
        Dev.Bar[0] = gBarPhys;
    }

    MmioW32(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)MmioR32(E1000_REG_ICR);

    if (!PciEnableMsi(&Dev, VEC_E1000, 0)) {
        DebugWrite("e1000: MSI failed; stay poll\n");
        return 0;
    }

    /* 立刻投递；RXT0 / RXDMT0 / LSC */
    MmioW32(E1000_REG_ITR, 0);
    MmioW32(E1000_REG_IMS, E1000_IMS_RX);
    gE1000UseIrq = 1;
    return 1;
}

/* 等 STATUS.LU；超时返回 0（Setup soft-fail，不挡桌面） */
int WaitLinkUp(void) {
    int Spin = 2000000;

    while (Spin-- > 0) {
        if (MmioR32(E1000_REG_STATUS) & E1000_STATUS_LU) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}
