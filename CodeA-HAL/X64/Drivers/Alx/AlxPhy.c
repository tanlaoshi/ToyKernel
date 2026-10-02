/*
 * AlxPhy.c — MDIO + 链路（PR-N-alx-2）
 *
 * 顺序对齐 Linux probe：ResetPhy → ResetMac → BMCR AN → 等链路（秒级）。
 */
#include "AlxPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

static int MdioIdle(void) {
    int i;
    for (i = 0; i < 120; i++) {
        if ((AlxMmioR32(ALX_MDIO) & ALX_MDIO_BUSY) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

int AlxMdioRead(UINT16 Reg, UINT16 *Out) {
    UINT32 Val;
    UINT32 Clk = ALX_MDIO_CLK_SEL_25MD128;

    if (!Out || !gAlxBar) {
        return 0;
    }
    if (gAlxLinkMbps != 0) {
        Clk = ALX_MDIO_CLK_SEL_25MD4;
    }
    Val = ALX_MDIO_SPRES_PRMBL |
          (Clk << ALX_MDIO_CLK_SEL_SHIFT) |
          ((UINT32)Reg << ALX_MDIO_REG_SHIFT) |
          ALX_MDIO_START | ALX_MDIO_OP_READ;
    AlxMmioW32(ALX_MDIO, Val);
    if (!MdioIdle()) {
        return 0;
    }
    *Out = (UINT16)(AlxMmioR32(ALX_MDIO) & 0xFFFFu);
    return 1;
}

int AlxMdioWrite(UINT16 Reg, UINT16 Val16) {
    UINT32 Val;
    UINT32 Clk = ALX_MDIO_CLK_SEL_25MD128;

    if (!gAlxBar) {
        return 0;
    }
    if (gAlxLinkMbps != 0) {
        Clk = ALX_MDIO_CLK_SEL_25MD4;
    }
    Val = ALX_MDIO_SPRES_PRMBL |
          (Clk << ALX_MDIO_CLK_SEL_SHIFT) |
          ((UINT32)Reg << ALX_MDIO_REG_SHIFT) |
          ((UINT32)Val16 << ALX_MDIO_DATA_SHIFT) |
          ALX_MDIO_START;
    AlxMmioW32(ALX_MDIO, Val);
    return MdioIdle();
}

static int ReadLinkOnce(UINT32 *MbpsOut, int *FullOut) {
    UINT16 Bmsr;
    UINT16 Giga;
    UINT32 Mbps = 0;
    int Full = 1;

    if (!AlxMdioRead(MII_BMSR, &Bmsr)) {
        return -1;
    }
    if (!AlxMdioRead(MII_BMSR, &Bmsr)) {
        return -1;
    }
    if ((Bmsr & BMSR_LSTATUS) == 0) {
        return 0;
    }
    if (!AlxMdioRead(ALX_MII_GIGA_PSSR, &Giga)) {
        return -1;
    }
    if ((Giga & ALX_GIGA_PSSR_SPD_DPLX_RESOLVED) == 0) {
        return 0;
    }
    switch (Giga & ALX_GIGA_PSSR_SPEED) {
    case ALX_GIGA_PSSR_1000MBS:
        Mbps = 1000;
        break;
    case ALX_GIGA_PSSR_100MBS:
        Mbps = 100;
        break;
    case ALX_GIGA_PSSR_10MBS:
        Mbps = 10;
        break;
    default:
        return 0;
    }
    Full = (Giga & ALX_GIGA_PSSR_DPLX) ? 1 : 0;
    if (MbpsOut) {
        *MbpsOut = Mbps;
    }
    if (FullOut) {
        *FullOut = Full;
    }
    return 1;
}

int AlxEnsureAutoneg(void) {
    UINT16 Adv;
    UINT16 Giga;
    UINT16 Cr;
    int GigaChip = (gAlxDid & 1u) ? 1 : 0;

    Adv = ADVERTISE_CSMA | ADVERTISE_10HALF | ADVERTISE_10FULL |
          ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_PAUSE;
    Giga = GigaChip ? ADVERTISE_1000FULL : 0;
    Cr = BMCR_RESET | BMCR_ANENABLE | BMCR_ANRESTART;

    if (!AlxMdioWrite(MII_ADVERTISE, Adv)) {
        return 0;
    }
    if (!AlxMdioWrite(MII_CTRL1000, Giga)) {
        return 0;
    }
    if (!AlxMdioWrite(MII_BMCR, Cr)) {
        return 0;
    }
    return 1;
}

static int PollLink(UINT32 *MbpsOut, int *FullOut, int Tries, UINT32 GapMs) {
    int i;
    int Rc;
    int SawMdioFail = 0;

    for (i = 0; i < Tries; i++) {
        Rc = ReadLinkOnce(MbpsOut, FullOut);
        if (Rc > 0) {
            return 1;
        }
        if (Rc < 0) {
            SawMdioFail = 1;
        }
        AlxStallMs(GapMs);
    }
    return SawMdioFail ? -1 : 0;
}

static void LogPhyDump(void) {
    UINT16 Bmsr = 0;
    UINT16 Giga = 0;
    UINT16 Bmcr = 0;
    char Hex[12];

    (void)AlxMdioRead(MII_BMCR, &Bmcr);
    (void)AlxMdioRead(MII_BMSR, &Bmsr);
    (void)AlxMdioRead(ALX_MII_GIGA_PSSR, &Giga);
    ToyLogNet("Boot: Alx PHY Bmcr=");
    HalSerialFormatHex(Hex, Bmcr, 4);
    ToyLogNet(Hex + 2);
    ToyLogNet(" Bmsr=");
    HalSerialFormatHex(Hex, Bmsr, 4);
    ToyLogNet(Hex + 2);
    ToyLogNet(" Pssr=");
    HalSerialFormatHex(Hex, Giga, 4);
    ToyLogNet(Hex + 2);
    ToyLogNet("\n");
}

int AlxWaitLink(UINT32 *MbpsOut, int *FullOut) {
    int Rc;

    /* PHY+MAC 复位后：先 BMCR AN，再等（约 4s） */
    ToyLogNet("Boot: Alx AN Restart\n");
    if (!AlxEnsureAutoneg()) {
        ToyLogNet("Boot: Alx AN Fail\n");
        LogPhyDump();
        return 0;
    }
    AlxStallMs(200);

    Rc = PollLink(MbpsOut, FullOut, 80, 50);
    if (Rc > 0) {
        return 1;
    }
    if (Rc < 0) {
        ToyLogNet("Boot: Alx MDIO Fail\n");
    }
    LogPhyDump();
    return 0;
}
