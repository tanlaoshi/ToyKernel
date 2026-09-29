/*
 * VirtioInputEv.c — evdev 解码 / 队列 / ring drain（PR-S3-virtioinput-1）
 */
#include "VirtioInputPrivate.h"
#include "VirtioInputDiag.h"
#include "HalVideo.h"

UINT8 LinuxKeyToHid(UINT16 Code) {
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

UINT8 LinuxModBit(UINT16 Code) {
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

void KbdPush(void) {
    UINT32 N = (gKbdHead + 1) % KBD_Q_SIZE;
    if (N == gKbdTail) {
        return;
    }
    gKbdQ[gKbdHead] = gKbdCur;
    gKbdHead = N;
    VirtioInputDiagNoteKeyboardPush();
}

void MousePush(void) {
    UINT32 N = (gMouseHead + 1) % MOUSE_Q_SIZE;
    HAL_MOUSE_REPORT R;
    UINT32 W = 800;
    UINT32 H = 600;
    const BOOT_INFO *Info = BootInfoGet();

    /* 逻辑分辨率（含 UI scale）；勿用 BootInfo 物理尺寸否则放大后钉右缘 */
    HalVideoGetSize(&W, &H);
    if (W == 0 || H == 0) {
        if (Info && Info->HorizontalResolution && Info->VerticalResolution) {
            W = Info->HorizontalResolution;
            H = Info->VerticalResolution;
        } else {
            W = 800;
            H = 600;
        }
    }
    if (N == gMouseTail) {
        return;
    }
    R.Buttons = gButtons;
    R.Wheel = gWheelAcc;
    R.Absolute = 0; /* 已换算为像素 */
    gWheelAcc = 0;
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
    VirtioInputDiagNoteMousePush();
}

void ApplyKey(UINT16 Code, INT32 Value) {
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

void RefillQueue(VIRTIO_MMIO_DEV *Dev, VIRTIO_INPUT_EVENT *Buf, UINT16 Count) {
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

UINT16 DrainDev(VIRTIO_MMIO_DEV *Dev, VIRTIO_INPUT_EVENT *Buf, int IsTab) {
    UINT16 Used;
    UINT16 Events = 0;

    if (!Dev || !Dev->Base) {
        return 0;
    }
    Used = *Dev->UsedIdx;
    while (Dev->LastUsed != Used) {
        UINT16 Id = (UINT16)Dev->UsedRing[Dev->LastUsed % Dev->QueueSize].Id;
        VIRTIO_INPUT_EVENT Ev = Buf[Id];
        UINT16 A;

        Events++;
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
        } else if (IsTab && Ev.Type == EV_REL) {
            /* PR-I1：滚轮；其它相对轴忽略（tablet 主路径仍是 ABS） */
            if (Ev.Code == REL_WHEEL) {
                INT32 Acc = (INT32)gWheelAcc + Ev.Value;
                if (Acc > 127) {
                    Acc = 127;
                }
                if (Acc < -128) {
                    Acc = -128;
                }
                gWheelAcc = (INT8)Acc;
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
    return Events;
}

