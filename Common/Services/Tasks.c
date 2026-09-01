/*
 * Tasks.c — 内核常驻任务：Shell / GUI / Worker
 */
#include "Tasks.h"
#include "Hal.h"
#include "Serial.h"
#include "XHCI.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Gui.h"
#include "Net.h"
#include "Udp.h"
#include "Tcp.h"
#include "Debug.h"

static volatile UINT32 gWorkerCount;

UINT32 WorkerLoopCount(void) {
    return gWorkerCount;
}

static void FeedHid(USB_KEYBOARD_REPORT *Report, USB_KEYBOARD_REPORT *Previous) {
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
            ConsoleOnEnter();
            continue;
        }
        if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT ||
            Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
            GuiOnArrowKey(Key);
            continue;
        }
        if (Key == HID_KEY_BACKSPACE) {
            ConsoleOnBackspace();
            continue;
        }

        char C = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
        if (C != 0) {
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
    USB_KEYBOARD_REPORT Report = {0};
    USB_KEYBOARD_REPORT Previous = {0};
    DebugWrite("shell task running (preemptive)\n");
    for (;;) {
        while (SerialDataReady()) {
            char C = SerialReadChar();
            if (C == '\r' || C == '\n') {
                ConsoleOnEnter();
            } else if (C == '\b' || C == 127) {
                ConsoleOnBackspace();
            } else if (C >= 32 && C <= 126) {
                ConsoleOnChar(C);
            }
        }
        HalCpuHalt();
        while (XhciDequeueKeyboard(&Report)) {
            FeedHid(&Report, &Previous);
            Previous = Report;
        }
        NetPoll();
        TcpPoll();
        {
            UDP_DATAGRAM Dg;
            while (UdpRecv(&Dg)) {
                char IpBuf[20];
                UINTN i;
                NetFormatIp(Dg.SrcIp, IpBuf, sizeof(IpBuf));
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
