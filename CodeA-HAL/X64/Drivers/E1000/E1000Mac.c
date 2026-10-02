/*
 * E1000Mac.c — NVM / RAL MAC（PR-S3-e1000probe-1）
 */
#include "E1000.h"
#include "E1000Private.h"
#include "Debug.h"
#include "Hal.h"

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
