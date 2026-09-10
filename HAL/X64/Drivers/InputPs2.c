/*
 * InputPs2.c — i8042 PS/2 键盘 → HID 报告（PR-H2）
 *
 * 仅在 xhci-hid 未绑定时 Bind。轮询 0x60/0x64。
 *
 * 笔记本内置键盘（如 ASUS N56VZ）复位后几乎总是 Scan Code Set 2；
 * 旧代码 F0/01 + Set1 解析会把 0xF0 断码当按键 → 乱码/次数不对。
 * 现固定 Set 2，关 8042 翻译，并丢弃 Aux（触摸板）字节。
 */
#include "Driver.h"
#include "DriverInput.h"
#include "Hal.h"
#include "Debug.h"
#include "InputPs2.h"
#include "VirtualMemory.h"
#include "SpinLock.h"
#include "ToySerialLog.h"

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define STATUS_OBF   (1u << 0)
#define STATUS_IBF   (1u << 1)
#define STATUS_MOUSE (1u << 5)

#define KBD_Q 16

static int gPs2Ready;
static UINT8 gExt;
static UINT8 gBreak; /* Set2：已见 0xF0，下一字节为 make 作 break */
static UINT8 gMods; /* HID modifier bits */
static HAL_KEYBOARD_REPORT gQ[KBD_Q];
static volatile UINT32 gRd;
static volatile UINT32 gWr;
static UINT8 gDown[256];
static SPIN_LOCK gPs2Lock;

static int Ps2StatusLooksDead(UINT8 St) {
    return St == 0xFF;
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

/* Scan Code Set 2 make → HID Usage（字母数字与常用符） */
static UINT8 Set2ToHid(UINT8 Sc) {
    switch (Sc) {
    case 0x1C: return 0x04; /* a */
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
    case 0x16: return 0x1E; /* 1 */
    case 0x1E: return 0x1F;
    case 0x26: return 0x20;
    case 0x25: return 0x21;
    case 0x2E: return 0x22;
    case 0x36: return 0x23;
    case 0x3D: return 0x24;
    case 0x3E: return 0x25;
    case 0x46: return 0x26;
    case 0x45: return 0x27; /* 0 */
    case 0x5A: return 0x28; /* Enter */
    case 0x76: return 0x29; /* Esc */
    case 0x66: return 0x2A; /* Backspace */
    case 0x0D: return 0x2B; /* Tab */
    case 0x29: return 0x2C; /* Space */
    case 0x4E: return 0x2D; /* - */
    case 0x55: return 0x2E; /* = */
    case 0x54: return 0x2F; /* [ */
    case 0x5B: return 0x30; /* ] */
    case 0x5D: return 0x31; /* \\ */
    case 0x4C: return 0x33; /* ; */
    case 0x52: return 0x34; /* ' */
    case 0x0E: return 0x35; /* ` */
    case 0x41: return 0x36; /* , */
    case 0x49: return 0x37; /* . */
    case 0x4A: return 0x38; /* / */
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

    if (Sc == 0x12 || Sc == 0x59) {
        gMods |= 0x02; /* L/R Shift */
        PushReport();
        return;
    }
    if (Sc == 0x14) {
        gMods |= 0x01; /* LCtrl */
        PushReport();
        return;
    }
    if (Sc == 0x11) {
        gMods |= 0x04; /* LAlt */
        PushReport();
        return;
    }
    Hid = Set2ToHid(Sc);
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
    while (Guard-- > 0) {
        UINT8 St = HalIoRead8(PS2_STATUS);
        UINT8 B;

        if (Ps2StatusLooksDead(St) || (St & STATUS_OBF) == 0) {
            break;
        }
        B = HalIoRead8(PS2_DATA);
        /* 触摸板/Aux 与键盘共用 0x60：必须丢掉，否则当键盘码 → 乱码 */
        if (St & STATUS_MOUSE) {
            continue;
        }
        if (B == 0xFA || B == 0xAA || B == 0xFE || B == 0xEE || B == 0x00 || B == 0xFF) {
            continue;
        }
        if (B == 0xE0) {
            gExt = 1;
            continue;
        }
        if (B == 0xE1) {
            gExt = 0;
            gBreak = 0;
            continue;
        }
        if (B == 0xF0) {
            gBreak = 1;
            continue;
        }
        if (gExt) {
            gExt = 0;
            gBreak = 0;
            continue;
        }
        if (gBreak) {
            gBreak = 0;
            OnBreak(B);
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

static int ExpectAck(void) {
    UINT8 Ack = 0;
    return KbdRead(&Ack) && Ack == 0xFA;
}

/*
 * CCB：开键盘、关鼠标时钟、关翻译（我们自己吃 Set2）。
 * Bit4=kbd disable, Bit5=mouse disable, Bit6=XT translation.
 */
static void ConfigureCcb(void) {
    UINT8 Ccb = 0x20;

    CtrlCmd(0x20);
    if (KbdRead(&Ccb)) {
        /* keep */
    }
    Ccb &= (UINT8)~(1u << 4);
    Ccb |= (UINT8)(1u << 5);
    Ccb &= (UINT8)~(1u << 6);
    Ccb &= (UINT8)~(1u << 0);
    Ccb &= (UINT8)~(1u << 1);
    CtrlCmd(0x60);
    KbdWrite(Ccb);
}

static int Ps2InitHw(void) {
    UINT8 Ack = 0;

    DrainOb(256);

    CtrlCmd(0xAD);
    CtrlCmd(0xA7);
    DrainOb(256);

    CtrlCmd(0xAA);
    if (!KbdRead(&Ack) || Ack != 0x55) {
        ToyLogDrv("boot: ps2-kbd self-test fail\n");
        return 0;
    }

    ConfigureCcb();
    CtrlCmd(0xAE);

    KbdWrite(0xFF);
    if (ExpectAck()) {
        (void)KbdRead(&Ack);
    }

    /* 明确 Set 2；勿再 F0/01 */
    KbdWrite(0xF0);
    if (ExpectAck()) {
        KbdWrite(0x02);
        (void)ExpectAck();
    }

    KbdWrite(0xF4);
    (void)ExpectAck();

    if (Ps2StatusLooksDead(HalIoRead8(PS2_STATUS))) {
        ToyLogDrv("boot: ps2-kbd died after init\n");
        return 0;
    }

    ToyLogDrv("boot: ps2-kbd keyboard (set2)\n");
    return 1;
}

static int Ps2DriverProbe(const TOY_DRIVER *Self, void *BusCtx, void **OutPriv) {
    (void)Self;
    (void)BusCtx;
    if (ToyDriverInputReady()) {
        return -1;
    }
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
        ToyLogDrv("boot: ps2-kbd probe failed\n");
        return -1;
    }
    gPs2Ready = 1;
    gRd = gWr = 0;
    gMods = 0;
    gExt = 0;
    gBreak = 0;
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
