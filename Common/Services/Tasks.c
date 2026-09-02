/*
 * Tasks.c — 内核常驻任务：Shell / GUI / Worker
 */
#include "Tasks.h"
#include "Hal.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Gui.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "Debug.h"

static volatile UINT32 gWorkerCount;

UINT32 WorkerLoopCount(void) {
    return gWorkerCount;
}

static void FeedHid(HAL_KEYBOARD_REPORT *Report, HAL_KEYBOARD_REPORT *Previous) {
    for (int i = 0; i < 6; i++) {
        UINT8 Key = Report->KeyCode[i];
        if (Key == 0) {
            continue;
        }
        int WasDown = 0;
        for (int j = 0; j < 6; j++) {
            if (Previous->KeyCode[j] == Key) {
                WasDown = 1;
                break;
            }
        }
        if (WasDown) {
            continue;
        }

        if (Key == HID_KEY_ENTER) {
            if (GuiShellAcceptsInput()) {
                ConsoleOnEnter();
            }
            continue;
        }
        if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT ||
            Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
            GuiOnArrowKey(Key);
            continue;
        }
        if (Key == HID_KEY_BACKSPACE) {
            if (GuiShellAcceptsInput()) {
                ConsoleOnBackspace();
            }
            continue;
        }
        if (Key == HID_KEY_CAPSLOCK) {
            HIDKeyboardToggleCapsLock();
            HalKeyboardSetLeds(HIDKeyboardGetLeds());
            continue;
        }

        char C = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (C != 0 && GuiShellAcceptsInput()) {
            ConsoleOnChar(C);
        }
    }
}

void GuiTask(void) {
    for (;;) {
        GuiPollMouse();
        HalCpuHalt();
    }
}

void ShellTask(void) {
    HAL_KEYBOARD_REPORT Report = {0};
    HAL_KEYBOARD_REPORT Previous = {0};
    DebugWrite("shell task running (preemptive)\n");
    for (;;) {
        while (HalSerialDataReady()) {
            char C = HalSerialReadChar();
            if (C == '\r' || C == '\n') {
                ConsoleOnEnter();
            } else if (C == '\b' || C == 127) {
                ConsoleOnBackspace();
            } else if (C >= 32 && C <= 126) {
                ConsoleOnChar(C);
            }
        }
        HalCpuHalt();
        while (HalKeyboardDequeue(&Report)) {
            FeedHid(&Report, &Previous);
            Previous = Report;
        }
        HalNetPoll();
        TcpPoll();
#ifdef TOY_LWIP
        LwIpPoll();
#endif
        {
            UDP_DATAGRAM Dg;
            while (UdpRecv(&Dg)) {
                char IpBuf[20];
                UINTN i;
                HalNetFormatIp(Dg.SrcIp, IpBuf, sizeof(IpBuf));
                ConsoleWrite("udp from ");
                ConsoleWrite(IpBuf);
                ConsoleWrite(":");
                ConsoleHex32(Dg.SrcPort);
                ConsoleWrite(" ");
                for (i = 0; i < Dg.Len; i++) {
                    char C = (char)Dg.Data[i];
                    if (C >= 32 && C <= 126) {
                        ConsoleOnChar(C);
                    }
                }
                ConsoleWrite("\n");
            }
        }
        GuiPollMouse();
    }
}

void WorkerTask(void) {
    for (;;) {
        gWorkerCount++;
        for (volatile int i = 0; i < 5000; i++) {
        }
        HalCpuHalt();
    }
}
