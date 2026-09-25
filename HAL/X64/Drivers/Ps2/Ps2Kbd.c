/*
 * Ps2Kbd.c — Scan Code Set 2 → HID 报告（PR-H-ps2-aux）
 */
#include "Ps2Private.h"

static UINT8 gExt;
static UINT8 gBreak;
static UINT8 gMods;
static HAL_KEYBOARD_REPORT gQ[PS2_KBD_Q];
static volatile UINT32 gRd;
static volatile UINT32 gWr;
static UINT8 gDown[256];

static UINT8 Set2ToHid(UINT8 Sc) {
    switch (Sc) {
    case 0x1C: return 0x04;
    case 0x32: return 0x05;
    case 0x21: return 0x06;
    case 0x23: return 0x07;
    case 0x24: return 0x08;
    case 0x2B: return 0x09;
    case 0x34: return 0x0A;
    case 0x33: return 0x0B;
    case 0x43: return 0x0C;
    case 0x3B: return 0x0D;
    case 0x42: return 0x0E;
    case 0x4B: return 0x0F;
    case 0x3A: return 0x10;
    case 0x31: return 0x11;
    case 0x44: return 0x12;
    case 0x4D: return 0x13;
    case 0x15: return 0x14;
    case 0x2D: return 0x15;
    case 0x1B: return 0x16;
    case 0x2C: return 0x17;
    case 0x3C: return 0x18;
    case 0x2A: return 0x19;
    case 0x1D: return 0x1A;
    case 0x22: return 0x1B;
    case 0x35: return 0x1C;
    case 0x1A: return 0x1D;
    case 0x16: return 0x1E;
    case 0x1E: return 0x1F;
    case 0x26: return 0x20;
    case 0x25: return 0x21;
    case 0x2E: return 0x22;
    case 0x36: return 0x23;
    case 0x3D: return 0x24;
    case 0x3E: return 0x25;
    case 0x46: return 0x26;
    case 0x45: return 0x27;
    case 0x5A: return 0x28;
    case 0x76: return 0x29;
    case 0x66: return 0x2A;
    case 0x0D: return 0x2B;
    case 0x29: return 0x2C;
    case 0x4E: return 0x2D;
    case 0x55: return 0x2E;
    case 0x54: return 0x2F;
    case 0x5B: return 0x30;
    case 0x5D: return 0x31;
    case 0x4C: return 0x33;
    case 0x52: return 0x34;
    case 0x0E: return 0x35;
    case 0x41: return 0x36;
    case 0x49: return 0x37;
    case 0x4A: return 0x38;
    default: return 0;
    }
}

static void PushReport(void) {
    HAL_KEYBOARD_REPORT R;
    UINT32 Next = (gWr + 1) % PS2_KBD_Q;
    int Slot = 0;
    int i;

    if (Next == gRd) {
        return;
    }
    R.ModifierKeys = gMods;
    R.Reserved = 0;
    for (i = 0; i < 6; i++) {
        R.KeyCode[i] = 0;
    }
    for (i = 0; i < 256 && Slot < 6; i++) {
        if (gDown[i]) {
            R.KeyCode[Slot++] = (UINT8)i;
        }
    }
    gQ[gWr] = R;
    gWr = Next;
}

static void OnMake(UINT8 Sc) {
    UINT8 Hid;

    if (Sc == 0x12 || Sc == 0x59) {
        gMods |= 0x02;
        PushReport();
        return;
    }
    if (Sc == 0x14) {
        gMods |= 0x01;
        PushReport();
        return;
    }
    if (Sc == 0x11) {
        gMods |= 0x04;
        PushReport();
        return;
    }
    Hid = Set2ToHid(Sc);
    if (Hid == 0 || gDown[Hid]) {
        return;
    }
    gDown[Hid] = 1;
    PushReport();
}

static void OnBreak(UINT8 Sc) {
    UINT8 Hid;

    if (Sc == 0x12 || Sc == 0x59) {
        gMods &= (UINT8)~0x02;
        PushReport();
        return;
    }
    if (Sc == 0x14) {
        gMods &= (UINT8)~0x01;
        PushReport();
        return;
    }
    if (Sc == 0x11) {
        gMods &= (UINT8)~0x04;
        PushReport();
        return;
    }
    Hid = Set2ToHid(Sc);
    if (Hid == 0 || !gDown[Hid]) {
        return;
    }
    gDown[Hid] = 0;
    PushReport();
}

void Ps2KbdResetState(void) {
    int i;
    gRd = gWr = 0;
    gMods = 0;
    gExt = 0;
    gBreak = 0;
    for (i = 0; i < 256; i++) {
        gDown[i] = 0;
    }
}

void Ps2KbdFeed(UINT8 B) {
    if (B == 0xFA || B == 0xAA || B == 0xFE || B == 0xEE || B == 0x00 || B == 0xFF) {
        return;
    }
    if (B == 0xE0) {
        gExt = 1;
        return;
    }
    if (B == 0xE1) {
        gExt = 0;
        gBreak = 0;
        return;
    }
    if (B == 0xF0) {
        gBreak = 1;
        return;
    }
    if (gExt) {
        gExt = 0;
        gBreak = 0;
        return;
    }
    if (gBreak) {
        gBreak = 0;
        OnBreak(B);
    } else {
        OnMake(B);
    }
}

int Ps2KbdDequeue(HAL_KEYBOARD_REPORT *Report) {
    if (!Report || !gPs2Ready || gRd == gWr) {
        return 0;
    }
    *Report = gQ[gRd];
    gRd = (gRd + 1) % PS2_KBD_Q;
    return 1;
}
