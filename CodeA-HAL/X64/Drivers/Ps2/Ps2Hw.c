/*
 * Ps2Hw.c — i8042 端口 / CCB / 键盘+Aux 上电（PR-H-ps2-aux）
 */
#include "Ps2Private.h"
#include "Hal.h"
#include "ToySerialLog.h"

int gPs2Ready;
int gPs2AuxReady;
SPIN_LOCK gPs2Lock;

int Ps2StatusLooksDead(UINT8 St) {
    return St == 0xFF;
}

void Ps2WaitIbFree(void) {
    int i;
    for (i = 0; i < 8000; i++) {
        UINT8 St = HalIoRead8(PS2_STATUS);
        if (Ps2StatusLooksDead(St) || (St & STATUS_IBF) == 0) {
            return;
        }
    }
}

void Ps2WaitOb(void) {
    int i;
    for (i = 0; i < 8000; i++) {
        UINT8 St = HalIoRead8(PS2_STATUS);
        if (Ps2StatusLooksDead(St)) {
            return;
        }
        if (St & STATUS_OBF) {
            return;
        }
    }
}

void Ps2CtrlCmd(UINT8 Cmd) {
    Ps2WaitIbFree();
    HalIoWrite8(PS2_CMD, Cmd);
}

void Ps2DataWrite(UINT8 Data) {
    Ps2WaitIbFree();
    HalIoWrite8(PS2_DATA, Data);
}

int Ps2DataRead(UINT8 *Out) {
    Ps2WaitOb();
    if (!(HalIoRead8(PS2_STATUS) & STATUS_OBF)) {
        return 0;
    }
    *Out = HalIoRead8(PS2_DATA);
    return 1;
}

void Ps2DrainOb(int Max) {
    int i;
    for (i = 0; i < Max; i++) {
        if ((HalIoRead8(PS2_STATUS) & STATUS_OBF) == 0) {
            return;
        }
        (void)HalIoRead8(PS2_DATA);
    }
}

int Ps2ExpectAck(void) {
    UINT8 Ack = 0;
    return Ps2DataRead(&Ack) && Ack == 0xFA;
}

int Ps2AuxReadByte(UINT8 *Out) {
    int Guard = 64;
    int Spin;

    if (!Out) {
        return 0;
    }
    *Out = 0;
    while (Guard-- > 0) {
        for (Spin = 0; Spin < 400000; Spin++) {
            UINT8 St = HalIoRead8(PS2_STATUS);
            if (Ps2StatusLooksDead(St)) {
                return 0;
            }
            if (St & STATUS_OBF) {
                UINT8 B = HalIoRead8(PS2_DATA);
                if (St & STATUS_MOUSE) {
                    *Out = B;
                    return 1;
                }
                break;
            }
            HalCpuRelax();
        }
    }
    return 0;
}

int Ps2AuxWrite(UINT8 Data) {
    UINT8 Ack = 0;
    int Try;

    for (Try = 0; Try < 3; Try++) {
        Ack = 0;
        Ps2DrainOb(32);
        Ps2CtrlCmd(0xD4);
        Ps2DataWrite(Data);
        if (Ps2AuxReadByte(&Ack) && Ack == 0xFA) {
            return 1;
        }
        if (Ack == 0xFE) {
            continue;
        }
        /* 键盘已关时可能无 mouse 位 */
        if (Ps2DataRead(&Ack) && Ack == 0xFA) {
            return 1;
        }
    }
    return 0;
}

void Ps2KbdPortEnable(int On) {
    Ps2CtrlCmd(On ? 0xAEu : 0xADu);
}

static void ConfigureCcb(int EnableAuxClock) {
    UINT8 Ccb = 0x20;

    Ps2CtrlCmd(0x20);
    if (Ps2DataRead(&Ccb)) {
        /* keep */
    }
    Ccb &= (UINT8)~(1u << 4);
    if (EnableAuxClock) {
        Ccb &= (UINT8)~(1u << 5);
    } else {
        Ccb |= (UINT8)(1u << 5);
    }
    Ccb &= (UINT8)~(1u << 6);
    Ccb &= (UINT8)~(1u << 0);
    Ccb &= (UINT8)~(1u << 1);
    Ps2CtrlCmd(0x60);
    Ps2DataWrite(Ccb);
}

static int InitKeyboardDevice(void) {
    UINT8 Ack = 0;

    Ps2DataWrite(0xFF);
    if (Ps2ExpectAck()) {
        (void)Ps2DataRead(&Ack);
    }

    Ps2DataWrite(0xF0);
    if (Ps2ExpectAck()) {
        Ps2DataWrite(0x02);
        (void)Ps2ExpectAck();
    }

    Ps2DataWrite(0xF4);
    (void)Ps2ExpectAck();
    return 1;
}

int Ps2InitHw(void) {
    UINT8 Ack = 0;

    gPs2AuxReady = 0;
    Ps2DrainOb(256);

    Ps2CtrlCmd(0xAD);
    Ps2CtrlCmd(0xA7);
    Ps2DrainOb(256);

    Ps2CtrlCmd(0xAA);
    if (!Ps2DataRead(&Ack) || Ack != 0x55) {
        ToyBootMarkUsb("Boot: PS2-KBD SelfTest Fail\n");
        return 0;
    }

    ConfigureCcb(1);
    /* 先 Aux（键盘口仍 AD），再开键盘——避免扫码抢走 Aux ACK */
    Ps2CtrlCmd(0xA8);
    if (Ps2AuxInitDevice()) {
        gPs2AuxReady = 1;
        ToyBootMarkUsb("Boot: PS2-AUX Mouse (Stream)\n");
    } else {
        ToyBootMarkUsb("Boot: PS2-AUX None (Ps2 / Ps2 Retry)\n");
    }

    Ps2CtrlCmd(0xAE);
    if (!InitKeyboardDevice()) {
        return 0;
    }
    if (Ps2StatusLooksDead(HalIoRead8(PS2_STATUS))) {
        ToyBootMarkUsb("Boot: PS2-KBD Died After Init\n");
        return 0;
    }
    ToyBootMarkUsb("Boot: PS2-KBD Keyboard (Set2)\n");
    return 1;
}
