/*
 * InputPs2.c — i8042 PS/2 键盘 → HID 报告（PR-H2）
 *
 * 仅在 xhci-hid 未绑定时 Bind。轮询 0x60/0x64；Scan Code Set 1（BIOS 翻译后常见）。
 */
#include "Driver.h"
#include "DriverInput.h"
#include "Hal.h"
#include "Debug.h"
#include "InputPs2.h"
#include "VirtualMemory.h"
#include "SpinLock.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define STATUS_OBF (1u << 0)
#define STATUS_IBF (1u << 1)

#define KBD_Q 16

static int gPs2Ready;
static UINT8 gExt;
static UINT8 gMods; /* HID modifier bits */
static HAL_KEYBOARD_REPORT gQ[KBD_Q];
static volatile UINT32 gRd;
static volatile UINT32 gWr;
static UINT8 gDown[256];
static SPIN_LOCK gPs2Lock; /* PR-S-ap */

static int Ps2StatusLooksDead(UINT8 St) {
    /* 无经典 8042 时端口常浮空为 0xFF */
    return St == 0xFF;
}

static void FlushObBounded(void) {
    int i;
    for (i = 0; i < 10000; i++) {
        UINT8 St = HalIoRead8(PS2_STATUS);
        if (Ps2StatusLooksDead(St) || (St & STATUS_OBF) == 0) {
            return;
        }
        (void)HalIoRead8(PS2_DATA);
    }
}

static void WaitIbFree(void) {
    for (int i = 0; i < 8000; i++) {
        UINT8 St = HalIoRead8(PS2_STATUS);
        if (Ps2StatusLooksDead(St) || (St & STATUS_IBF) == 0) {
            return;
        }
    }
}

static void WaitOb(void) {
    for (int i = 0; i < 8000; i++) {
        UINT8 St = HalIoRead8(PS2_STATUS);
        if (Ps2StatusLooksDead(St)) {
            return;
        }
        if (St & STATUS_OBF) {
            return;
        }
    }
}

static void CtrlCmd(UINT8 Cmd) {
    WaitIbFree();
    HalIoWrite8(PS2_CMD, Cmd);
}

static void KbdWrite(UINT8 Data) {
    WaitIbFree();
    HalIoWrite8(PS2_DATA, Data);
}

static int KbdRead(UINT8 *Out) {
    WaitOb();
    if (!(HalIoRead8(PS2_STATUS) & STATUS_OBF)) {
        return 0;
    }
    *Out = HalIoRead8(PS2_DATA);
    return 1;
}

/* Scan Code Set 1 make → HID Usage（子集） */
static UINT8 Set1ToHid(UINT8 Sc) {
    switch (Sc) {
    case 0x1E: return 0x04; /* a */
    case 0x30: return 0x05;
    case 0x2E: return 0x06;
    case 0x20: return 0x07;
    case 0x12: return 0x08;
    case 0x21: return 0x09;
    case 0x22: return 0x0A;
    case 0x23: return 0x0B;
    case 0x17: return 0x0C;
    case 0x24: return 0x0D;
    case 0x25: return 0x0E;
    case 0x26: return 0x0F;
    case 0x32: return 0x10;
    case 0x31: return 0x11;
    case 0x18: return 0x12;
    case 0x19: return 0x13;
    case 0x10: return 0x14;
    case 0x13: return 0x15;
    case 0x1F: return 0x16;
    case 0x14: return 0x17;
    case 0x16: return 0x18;
    case 0x2F: return 0x19;
    case 0x11: return 0x1A;
    case 0x2D: return 0x1B;
    case 0x15: return 0x1C;
    case 0x2C: return 0x1D;
    case 0x02: return 0x1E; /* 1 */
    case 0x03: return 0x1F;
    case 0x04: return 0x20;
    case 0x05: return 0x21;
    case 0x06: return 0x22;
    case 0x07: return 0x23;
    case 0x08: return 0x24;
    case 0x09: return 0x25;
    case 0x0A: return 0x26;
    case 0x0B: return 0x27; /* 0 */
    case 0x1C: return 0x28; /* Enter */
    case 0x01: return 0x29; /* Esc */
    case 0x0E: return 0x2A; /* Backspace */
    case 0x0F: return 0x2B; /* Tab */
    case 0x39: return 0x2C; /* Space */
    case 0x0C: return 0x2D; /* - */
    case 0x0D: return 0x2E; /* = */
    case 0x1A: return 0x2F; /* [ */
    case 0x1B: return 0x30; /* ] */
    case 0x2B: return 0x31; /* \\ */
    case 0x27: return 0x33; /* ; */
    case 0x28: return 0x34; /* ' */
    case 0x29: return 0x35; /* ` */
    case 0x33: return 0x36; /* , */
    case 0x34: return 0x37; /* . */
    case 0x35: return 0x38; /* / */
    default: return 0;
    }
}

static void PushReport(void) {
    HAL_KEYBOARD_REPORT R;
    UINT32 Next = (gWr + 1) % KBD_Q;
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

    if (Sc == 0x2A || Sc == 0x36) {
        gMods |= 0x02; /* Left/Right Shift → LeftShift bit for ASCII */
        PushReport();
        return;
    }
    if (Sc == 0x1D) {
        gMods |= 0x01; /* Ctrl */
        PushReport();
        return;
    }
    if (Sc == 0x38) {
        gMods |= 0x04; /* Alt */
        PushReport();
        return;
    }
    Hid = Set1ToHid(Sc);
    if (Hid == 0) {
        return;
    }
    if (!gDown[Hid]) {
        gDown[Hid] = 1;
        PushReport();
    }
}

static void OnBreak(UINT8 Sc) {
    UINT8 Hid;

    if (Sc == 0x2A || Sc == 0x36) {
        gMods &= (UINT8)~0x02;
        PushReport();
        return;
    }
    if (Sc == 0x1D) {
        gMods &= (UINT8)~0x01;
        PushReport();
        return;
    }
    if (Sc == 0x38) {
        gMods &= (UINT8)~0x04;
        PushReport();
        return;
    }
    Hid = Set1ToHid(Sc);
    if (Hid == 0) {
        return;
    }
    if (gDown[Hid]) {
        gDown[Hid] = 0;
        PushReport();
    }
}

static void Ps2Poll(void) {
    int Guard = 64;

    SpinLockAcquire(&gPs2Lock);
    while (Guard-- > 0 && (HalIoRead8(PS2_STATUS) & STATUS_OBF)) {
        UINT8 B = HalIoRead8(PS2_DATA);
        if (B == 0xE0) {
            gExt = 1;
            continue;
        }
        if (B == 0xE1) {
            gExt = 0;
            continue;
        }
        if (gExt) {
            gExt = 0;
            continue;
        }
        if (B & 0x80) {
            OnBreak((UINT8)(B & 0x7F));
        } else {
            OnMake(B);
        }
    }
    SpinLockRelease(&gPs2Lock);
}

static int Ps2KeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    int Ok = 0;

    SpinLockAcquire(&gPs2Lock);
    if (Report && gPs2Ready && gRd != gWr) {
        *Report = gQ[gRd];
        gRd = (gRd + 1) % KBD_Q;
        Ok = 1;
    }
    SpinLockRelease(&gPs2Lock);
    return Ok;
}

static int Ps2MousePresent(void) {
    return 0;
}

static int Ps2MouseDequeue(HAL_MOUSE_REPORT *Report) {
    (void)Report;
    return 0;
}

static const INPUT_BACKEND gPs2Backend = {
    .Poll = Ps2Poll,
    .KeyboardDequeue = Ps2KeyboardDequeue,
    .KeyboardSetLeds = 0,
    .MousePresent = Ps2MousePresent,
    .MouseDequeue = Ps2MouseDequeue,
};

static void DrainOb(int Max) {
    int i;
    for (i = 0; i < Max; i++) {
        if ((HalIoRead8(PS2_STATUS) & STATUS_OBF) == 0) {
            return;
        }
        (void)HalIoRead8(PS2_DATA);
    }
}

static int Ps2InitHw(void) {
    UINT8 Ack = 0;
    UINT8 St;

    /* 排空杂字节（须有上限：无 8042 时 STATUS 常为 0xFF，OBF 永真） */
    DrainOb(256);

    CtrlCmd(0xAD); /* disable kbd */
    CtrlCmd(0xA7); /* disable mouse */
    DrainOb(256);

    CtrlCmd(0xAA);
    if (!KbdRead(&Ack) || Ack != 0x55) {
        /* 真机无键或无 8042：快速失败，勿继续 reset 长序列 */
        HalSerialWrite("boot: ps2-kbd self-test fail\n");
        return 0;
    }

    CtrlCmd(0xAE);
    KbdWrite(0xFF);
    if (KbdRead(&Ack) && Ack == 0xFA) {
        (void)KbdRead(&Ack);
    }

    KbdWrite(0xF0);
    if (KbdRead(&Ack) && Ack == 0xFA) {
        KbdWrite(0x01);
        (void)KbdRead(&Ack);
    }

    KbdWrite(0xF4);
    (void)KbdRead(&Ack);

    if (Ps2StatusLooksDead(HalIoRead8(PS2_STATUS))) {
        HalSerialWrite("boot: ps2-kbd died after init\n");
        return 0;
    }

    HalSerialWrite("boot: ps2-kbd keyboard\n");
    return 1;
}

static int Ps2DriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;
    /* USB HID 已就绪则不再抢 Input；控制器起来但无键盘时仍可试 PS/2 */
    if (ToyDriverInputReady()) {
        return -1;
    }
    /*
     * 与 xHCI 一样：等 VMM 后再 Probe。
     * InitDriver 早 Probe 时 xHCI 会跳过，若此时 PS/2 浮空 0xFF 会死等，
     * 且会抢在亮屏之前；推迟到 HalUsbInit / Input 类再试。
     */
    if (!VirtualMemoryEnabled()) {
        return -1;
    }
    if (gPs2Ready) {
        if (OutPriv) {
            *OutPriv = 0;
        }
        return 0;
    }
    if (!Ps2InitHw()) {
        HalSerialWrite("boot: ps2-kbd probe failed\n");
        return -1;
    }
    gPs2Ready = 1;
    gRd = gWr = 0;
    gMods = 0;
    gExt = 0;
    {
        int i;
        for (i = 0; i < 256; i++) {
            gDown[i] = 0;
        }
    }
    if (OutPriv) {
        *OutPriv = 0;
    }
    return 0;
}

static int Ps2DriverBind(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    if (ToyDriverInputReady()) {
        return -1;
    }
    return ToyDriverInputAttach(&gPs2Backend);
}

static void Ps2DriverRemove(TOY_DRIVER_INSTANCE *Inst) {
    (void)Inst;
    gPs2Ready = 0;
}

static const TOY_DRIVER gPs2Driver = {
    .Name = "ps2-kbd",
    .Class = TOY_DRIVER_CLASS_INPUT,
    .Match = 0,
    .Probe = Ps2DriverProbe,
    .Bind = Ps2DriverBind,
    .Remove = Ps2DriverRemove,
};

void InputPs2Register(void) {
    (void)ToyDriverRegister(&gPs2Driver);
}
