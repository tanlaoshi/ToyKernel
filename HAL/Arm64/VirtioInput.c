/*
 * VirtioInput.c — virtio-input MMIO：键盘 + tablet（PR-V3）
 *
 * 事件为 Linux evdev；键码转 HID Usage 供 Common Tasks/Gui。
 */
#include "VirtioInput.h"
#include "VirtioMmio.h"
#include "HalSerial.h"
#include "PhysicalMemory.h"
#include "BootInfo.h"

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03

#define ABS_X 0x00
#define ABS_Y 0x01

#define VIRTIO_INPUT_CFG_ID_NAME 0x01
#define VIRTIO_INPUT_CFG_ABS     0x03
#define VIRTIO_INPUT_CFG_SIZE    0x100u

typedef struct {
    UINT16 Type;
    UINT16 Code;
    INT32  Value;
} __attribute__((packed)) VIRTIO_INPUT_EVENT;

typedef struct {
    UINT8 Select;
    UINT8 Subsel;
    UINT8 Size;
    UINT8 Reserved[5];
    UINT8 Data[128];
} __attribute__((packed)) VIRTIO_INPUT_CFG;

#define KBD_Q_SIZE 16
#define MOUSE_Q_SIZE 32

static VIRTIO_MMIO_DEV gKbd;
static VIRTIO_MMIO_DEV gTab;
static int gKbdOn;
static int gTabOn;

static HAL_KEYBOARD_REPORT gKbdQ[KBD_Q_SIZE];
static UINT32 gKbdHead;
static UINT32 gKbdTail;
static HAL_KEYBOARD_REPORT gKbdCur;

static HAL_MOUSE_REPORT gMouseQ[MOUSE_Q_SIZE];
static UINT32 gMouseHead;
static UINT32 gMouseTail;
static INT32 gAbsMinX;
static INT32 gAbsMaxX;
static INT32 gAbsMinY;
static INT32 gAbsMaxY;
static INT32 gAbsX;
static INT32 gAbsY;
static UINT8 gButtons;
static int gHaveAbs;

static VIRTIO_INPUT_EVENT *gKbdEvBuf;
static VIRTIO_INPUT_EVENT *gTabEvBuf;

/* Linux KEY_* → HID Usage（子集） */
static UINT8 LinuxKeyToHid(UINT16 Code) {
    if (Code >= 2 && Code <= 11) {
        /* KEY_1..KEY_0 */
        static const UINT8 Dig[10] = {
            0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
        };
        return Dig[Code - 2];
    }
    if (Code >= 16 && Code <= 25) {
        /* Q..P */
        static const UINT8 Row[10] = {
            0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13
        };
        return Row[Code - 16];
    }
    if (Code >= 30 && Code <= 38) {
        /* A..L */
        static const UINT8 Row[9] = {
            0x04, 0x16, 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F
        };
        return Row[Code - 30];
    }
    if (Code >= 44 && Code <= 50) {
        /* Z..M */
        static const UINT8 Row[7] = {
            0x1D, 0x1B, 0x06, 0x19, 0x05, 0x11, 0x10
        };
        return Row[Code - 44];
    }
    switch (Code) {
    case 14:  return 0x2A; /* BACKSPACE */
    case 15:  return 0x2B; /* TAB */
    case 28:  return 0x28; /* ENTER */
    case 57:  return 0x2C; /* SPACE */
    case 1:   return 0x29; /* ESC */
    case 111: return 0x4C; /* DELETE */
    case 105: return 0x50; /* LEFT */
    case 106: return 0x4F; /* RIGHT */
    case 103: return 0x52; /* UP */
    case 108: return 0x51; /* DOWN */
    case 12:  return 0x2D; /* - */
    case 13:  return 0x2E; /* = */
    case 26:  return 0x2F; /* [ */
    case 27:  return 0x30; /* ] */
    case 39:  return 0x33; /* ; */
    case 40:  return 0x34; /* ' */
    case 41:  return 0x35; /* ` */
    case 43:  return 0x31; /* \ */
    case 51:  return 0x36; /* , */
    case 52:  return 0x37; /* . */
    case 53:  return 0x38; /* / */
    case 58:  return 0x39; /* CAPSLOCK */
    default:  return 0;
    }
}

static UINT8 LinuxModBit(UINT16 Code) {
    switch (Code) {
    case 29:  return 0x01; /* LCTRL */
    case 42:  return 0x02; /* LSHIFT */
    case 56:  return 0x04; /* LALT */
    case 125: return 0x08; /* LGUI */
    case 97:  return 0x10; /* RCTRL */
    case 54:  return 0x20; /* RSHIFT */
    case 100: return 0x40; /* RALT */
    default:  return 0;
    }
}

static void KbdPush(void) {
    UINT32 N = (gKbdHead + 1) % KBD_Q_SIZE;
    if (N == gKbdTail) {
        return;
    }
    gKbdQ[gKbdHead] = gKbdCur;
    gKbdHead = N;
}

static void MousePush(void) {
    UINT32 N = (gMouseHead + 1) % MOUSE_Q_SIZE;
    HAL_MOUSE_REPORT R;
    UINT32 W = 800;
    UINT32 H = 600;
    const BOOT_INFO *Info = BootInfoGet();

    if (Info && Info->HorizontalResolution && Info->VerticalResolution) {
        W = Info->HorizontalResolution;
        H = Info->VerticalResolution;
    }
    if (N == gMouseTail) {
        return;
    }
    R.Buttons = gButtons;
    if (gHaveAbs && gAbsMaxX > gAbsMinX && gAbsMaxY > gAbsMinY) {
        R.X = (UINT32)(((INT64)(gAbsX - gAbsMinX) * (INT64)(W - 1)) /
                       (INT64)(gAbsMaxX - gAbsMinX));
        R.Y = (UINT32)(((INT64)(gAbsY - gAbsMinY) * (INT64)(H - 1)) /
                       (INT64)(gAbsMaxY - gAbsMinY));
    } else {
        R.X = 0;
        R.Y = 0;
    }
    gMouseQ[gMouseHead] = R;
    gMouseHead = N;
}

static void ApplyKey(UINT16 Code, INT32 Value) {
    UINT8 Mod = LinuxModBit(Code);
    UINT8 Hid;
    int i;

    if (Mod) {
        if (Value) {
            gKbdCur.ModifierKeys |= Mod;
        } else {
            gKbdCur.ModifierKeys &= (UINT8)~Mod;
        }
        KbdPush();
        return;
    }
    Hid = LinuxKeyToHid(Code);
    if (Hid == 0) {
        return;
    }
    if (Value) {
        for (i = 0; i < 6; i++) {
            if (gKbdCur.KeyCode[i] == Hid) {
                return;
            }
        }
        for (i = 0; i < 6; i++) {
            if (gKbdCur.KeyCode[i] == 0) {
                gKbdCur.KeyCode[i] = Hid;
                break;
            }
        }
    } else {
        for (i = 0; i < 6; i++) {
            if (gKbdCur.KeyCode[i] == Hid) {
                int j;
                for (j = i; j < 5; j++) {
                    gKbdCur.KeyCode[j] = gKbdCur.KeyCode[j + 1];
                }
                gKbdCur.KeyCode[5] = 0;
                break;
            }
        }
    }
    KbdPush();
}

static void RefillQueue(VIRTIO_MMIO_DEV *Dev, VIRTIO_INPUT_EVENT *Buf, UINT16 Count) {
    UINT16 i;
    for (i = 0; i < Count; i++) {
        Dev->Desc[i].Addr = (UINT64)(UINTN)(Buf + i);
        Dev->Desc[i].Len = sizeof(VIRTIO_INPUT_EVENT);
        Dev->Desc[i].Flags = VRING_DESC_F_WRITE;
        Dev->Desc[i].Next = 0;
        Dev->AvailRing[i] = i;
    }
    __asm__ volatile("" ::: "memory");
    *Dev->AvailIdx = Count;
    Dev->NextAvail = Count;
    VirtioMmioNotify(Dev);
}

static void DrainDev(VIRTIO_MMIO_DEV *Dev, VIRTIO_INPUT_EVENT *Buf, int IsTab) {
    UINT16 Used;

    if (!Dev || !Dev->Base) {
        return;
    }
    Used = *Dev->UsedIdx;
    while (Dev->LastUsed != Used) {
        UINT16 Id = (UINT16)Dev->UsedRing[Dev->LastUsed % Dev->QueueSize].Id;
        VIRTIO_INPUT_EVENT Ev = Buf[Id];
        UINT16 A;

        if (Ev.Type == EV_KEY) {
            if (!IsTab) {
                ApplyKey(Ev.Code, Ev.Value);
            } else if (Ev.Code == 0x110 || Ev.Code == 0x111 || Ev.Code == 0x112) {
                /* BTN_LEFT/RIGHT/MIDDLE */
                UINT8 Bit = (Ev.Code == 0x110) ? 1u : (Ev.Code == 0x111) ? 2u : 4u;
                if (Ev.Value) {
                    gButtons |= Bit;
                } else {
                    gButtons &= (UINT8)~Bit;
                }
                MousePush();
            }
        } else if (IsTab && Ev.Type == EV_ABS) {
            if (Ev.Code == ABS_X) {
                gAbsX = Ev.Value;
                gHaveAbs = 1;
            } else if (Ev.Code == ABS_Y) {
                gAbsY = Ev.Value;
                gHaveAbs = 1;
            }
        } else if (IsTab && Ev.Type == EV_SYN) {
            MousePush();
        }

        A = Dev->NextAvail;
        Dev->AvailRing[A % Dev->QueueSize] = Id;
        __asm__ volatile("" ::: "memory");
        *Dev->AvailIdx = (UINT16)(A + 1);
        Dev->NextAvail = (UINT16)(A + 1);

        Dev->LastUsed = (UINT16)(Dev->LastUsed + 1);
    }
    VirtioMmioAckInterrupt(Dev);
    VirtioMmioNotify(Dev);
}

static void ReadAbsInfo(UINT64 Base, UINT8 Axis, INT32 *Min, INT32 *Max) {
    volatile VIRTIO_INPUT_CFG *Cfg =
        (volatile VIRTIO_INPUT_CFG *)(UINTN)(Base + VIRTIO_INPUT_CFG_SIZE);
    INT32 *Vals;

    Cfg->Select = VIRTIO_INPUT_CFG_ABS;
    Cfg->Subsel = Axis;
    __asm__ volatile("" ::: "memory");
    if (Cfg->Size < 16) {
        *Min = 0;
        *Max = 32767;
        return;
    }
    Vals = (INT32 *)(void *)Cfg->Data;
    *Min = Vals[0];
    *Max = Vals[1];
}

typedef struct {
    UINT64 KbdBase;
    UINT64 TabBase;
} IN_SCAN;

static void InScanCb(UINT64 Base, UINT32 DeviceId, void *Ctx) {
    IN_SCAN *S = (IN_SCAN *)Ctx;
    volatile VIRTIO_INPUT_CFG *Cfg;
    char Name[64];
    UINT32 i;

    if (DeviceId != VIRTIO_DEV_INPUT) {
        return;
    }
    Cfg = (volatile VIRTIO_INPUT_CFG *)(UINTN)(Base + VIRTIO_INPUT_CFG_SIZE);
    Cfg->Select = VIRTIO_INPUT_CFG_ID_NAME;
    Cfg->Subsel = 0;
    __asm__ volatile("" ::: "memory");
    for (i = 0; i < 63 && i < Cfg->Size; i++) {
        Name[i] = (char)Cfg->Data[i];
    }
    Name[i] = 0;
    /* QEMU：名含 Keyboard / Tablet */
    if (S->KbdBase == 0) {
        for (i = 0; Name[i]; i++) {
            if ((Name[i] == 'K' || Name[i] == 'k') && Name[i + 1] == 'e' &&
                Name[i + 2] == 'y') {
                S->KbdBase = Base;
                return;
            }
        }
    }
    if (S->TabBase == 0) {
        for (i = 0; Name[i]; i++) {
            if ((Name[i] == 'T' || Name[i] == 't') && Name[i + 1] == 'a' &&
                Name[i + 2] == 'b') {
                S->TabBase = Base;
                return;
            }
        }
    }
    /* 无名时按发现顺序：先键盘后 tablet */
    if (S->KbdBase == 0) {
        S->KbdBase = Base;
    } else if (S->TabBase == 0 && Base != S->KbdBase) {
        S->TabBase = Base;
    }
}

int VirtioInputInit(void) {
    IN_SCAN S;
    UINT8 *Page;

    S.KbdBase = 0;
    S.TabBase = 0;
    VirtioMmioScan(InScanCb, &S);

    Page = (UINT8 *)PhysicalMemoryAllocatePages(2);
    if (!Page) {
        return -1;
    }
    gKbdEvBuf = (VIRTIO_INPUT_EVENT *)(UINTN)Page;
    gTabEvBuf = (VIRTIO_INPUT_EVENT *)(UINTN)(Page + PAGE_SIZE);

    if (S.KbdBase) {
        if (VirtioMmioSetupQueue(&gKbd, S.KbdBase, VIRTIO_DEV_INPUT, 8, 0) == 0) {
            RefillQueue(&gKbd, gKbdEvBuf, gKbd.QueueSize);
            gKbdOn = 1;
            HalSerialWrite("boot: virtio-input keyboard\n");
        }
    }
    if (S.TabBase) {
        if (VirtioMmioSetupQueue(&gTab, S.TabBase, VIRTIO_DEV_INPUT, 8, 0) == 0) {
            ReadAbsInfo(S.TabBase, ABS_X, &gAbsMinX, &gAbsMaxX);
            ReadAbsInfo(S.TabBase, ABS_Y, &gAbsMinY, &gAbsMaxY);
            RefillQueue(&gTab, gTabEvBuf, gTab.QueueSize);
            gTabOn = 1;
            HalSerialWrite("boot: virtio-input tablet\n");
        }
    }
    return (gKbdOn || gTabOn) ? 0 : -1;
}

void VirtioInputPoll(void) {
    if (gKbdOn) {
        DrainDev(&gKbd, gKbdEvBuf, 0);
    }
    if (gTabOn) {
        DrainDev(&gTab, gTabEvBuf, 1);
    }
}

int VirtioInputKeyboardDequeue(HAL_KEYBOARD_REPORT *Report) {
    if (!Report || gKbdTail == gKbdHead) {
        return 0;
    }
    *Report = gKbdQ[gKbdTail];
    gKbdTail = (gKbdTail + 1) % KBD_Q_SIZE;
    return 1;
}

int VirtioInputMousePresent(void) {
    return gTabOn;
}

int VirtioInputMouseDequeue(HAL_MOUSE_REPORT *Report) {
    if (!Report || gMouseTail == gMouseHead) {
        return 0;
    }
    *Report = gMouseQ[gMouseTail];
    gMouseTail = (gMouseTail + 1) % MOUSE_Q_SIZE;
    return 1;
}
