/*
 * Ps2Aux.c — Aux 流模式 3B 包 → 屏幕像素（PR-H-ps2-aux）
 */
#include "Ps2Private.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HalSerial.h"

static UINT8 gPkt[3];
static UINT8 gPktN;
static UINT32 gMx[PS2_MOUSE_Q];
static UINT32 gMy[PS2_MOUSE_Q];
static UINT8 gMb[PS2_MOUSE_Q];
static INT8 gMw[PS2_MOUSE_Q];
static volatile UINT32 gMRd;
static volatile UINT32 gMWr;
static int gAbsX;
static int gAbsY;
static int gAbsInit;
static UINT32 gPktOk;
static UINT32 gByteIn;
const char *gPs2Fail = "init";
UINT8 gPs2LastRx;
UINT8 gPs2DevId;

void Ps2AuxResetState(void) {
    gPktN = 0;
    gMRd = gMWr = 0;
    gAbsInit = 0;
    gPktOk = 0;
    gByteIn = 0;
}

void Ps2AuxHandoffDesktop(UINT32 CursorX, UINT32 CursorY) {
    gAbsX = (int)CursorX;
    gAbsY = (int)CursorY;
    gAbsInit = 1;
}

UINT32 Ps2AuxPktCount(void) {
    return gPktOk;
}

UINT32 Ps2AuxByteCount(void) {
    return gByteIn;
}

static void PushMouse(int Dx, int Dy, UINT8 Btn) {
    UINT32 Next = (gMWr + 1) % PS2_MOUSE_Q;
    UINT32 Sw = 0;
    UINT32 Sh = 0;
    int MaxX;
    int MaxY;

    if (Next == gMRd) {
        return;
    }
    HalVideoGetSize(&Sw, &Sh);
    if (Sw == 0) {
        Sw = 1024;
    }
    if (Sh == 0) {
        Sh = 768;
    }
    MaxX = (int)(Sw > 0 ? Sw - 1 : 0);
    MaxY = (int)(Sh > 0 ? Sh - 1 : 0);
    if (!gAbsInit) {
        gAbsX = (int)(Sw / 2);
        gAbsY = (int)(Sh / 2);
        gAbsInit = 1;
    }
    gAbsX += Dx * PS2_SCALE;
    gAbsY -= Dy * PS2_SCALE;
    if (gAbsX < 0) {
        gAbsX = 0;
    }
    if (gAbsY < 0) {
        gAbsY = 0;
    }
    if (gAbsX > MaxX) {
        gAbsX = MaxX;
    }
    if (gAbsY > MaxY) {
        gAbsY = MaxY;
    }
    gMx[gMWr] = (UINT32)gAbsX;
    gMy[gMWr] = (UINT32)gAbsY;
    gMb[gMWr] = Btn;
    gMw[gMWr] = 0;
    gMWr = Next;
    gPktOk++;
}

void Ps2AuxFeed(UINT8 B) {
    int Dx;
    int Dy;
    UINT8 Btn;

    gByteIn++;
    if (!gPs2AuxReady) {
        return;
    }
    if (gPktN == 0) {
        if ((B & 0x08u) == 0) {
            return;
        }
        gPkt[0] = B;
        gPktN = 1;
        return;
    }
    gPkt[gPktN++] = B;
    if (gPktN < 3) {
        return;
    }
    gPktN = 0;
    if (gPkt[0] & 0xC0u) {
        return;
    }
    Dx = (int)gPkt[1];
    Dy = (int)gPkt[2];
    if (gPkt[0] & 0x10u) {
        Dx -= 256;
    }
    if (gPkt[0] & 0x20u) {
        Dy -= 256;
    }
    Btn = (UINT8)(gPkt[0] & 0x07u);
    PushMouse(Dx, Dy, Btn);
}

int Ps2AuxPresent(void) {
    return gPs2AuxReady;
}

int Ps2AuxDequeue(HAL_MOUSE_REPORT *Report) {
    if (!Report || !gPs2AuxReady || gMRd == gMWr) {
        return 0;
    }
    Report->X = gMx[gMRd];
    Report->Y = gMy[gMRd];
    Report->Buttons = gMb[gMRd];
    Report->Wheel = gMw[gMRd];
    Report->Absolute = 0;
    gMRd = (gMRd + 1) % PS2_MOUSE_Q;
    return 1;
}

static void BusyDelay(int N) {
    while (N-- > 0) {
        HalCpuRelax();
    }
}

static int EnableStream(void) {
    gPs2Fail = "f4";
    if (!Ps2AuxWrite(0xF4)) {
        return 0;
    }
    Ps2AuxResetState();
    gPs2Fail = "ok";
    return 1;
}

int Ps2AuxInitDevice(void) {
    UINT8 Bat = 0;
    int GotReset = 0;

    gPs2LastRx = 0;
    gPs2DevId = 0xFF;
    Ps2KbdPortEnable(0);
    Ps2DrainOb(64);
    BusyDelay(100000);

    /* A9 仅参考；笔记本触控板常非 00 */
    gPs2Fail = "a9";
    Ps2CtrlCmd(0xA9);
    if (Ps2DataRead(&Bat)) {
        gPs2LastRx = Bat;
    }
    Ps2DrainOb(16);
    BusyDelay(100000);

    gPs2Fail = "ff";
    if (Ps2AuxWrite(0xFF)) {
        BusyDelay(400000);
        gPs2Fail = "bat";
        if (Ps2AuxReadByte(&Bat) || Ps2DataRead(&Bat)) {
            gPs2LastRx = Bat;
            if (Bat == 0xAA ||
                (Bat == 0xFA && (Ps2AuxReadByte(&Bat) || Ps2DataRead(&Bat)) &&
                 Bat == 0xAA)) {
                GotReset = 1;
                gPs2LastRx = Bat;
                if (Ps2AuxReadByte(&gPs2DevId) || Ps2DataRead(&gPs2DevId)) {
                    /* id */
                }
            }
        }
    }

    if (GotReset) {
        (void)Ps2AuxWrite(0xF6);
        BusyDelay(80000);
        if (EnableStream()) {
            Ps2KbdPortEnable(1);
            return 1;
        }
    }

    /* BIOS 可能已复位：跳过 FF，直接开报告（Synaptics 常见） */
    gPs2Fail = "f4only";
    Ps2DrainOb(32);
    BusyDelay(100000);
    if (EnableStream()) {
        Ps2KbdPortEnable(1);
        return 1;
    }

    Ps2KbdPortEnable(1);
    return 0;
}
