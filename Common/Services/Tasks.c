/*
 * Tasks.c — 内核常驻任务：Shell / GUI / Worker
 */
#include "Tasks.h"
#include "Hal.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Gui.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "Debug.h"
#include "ShellCommands.h"

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

        /* Settings 文字菜单：数字选择 / Esc 返回 */
        if (SettingsUiIsFocused()) {
            if (Key == HID_KEY_ESCAPE) {
                SettingsUiOnEscape();
                continue;
            }
            if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT ||
                Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
                GuiOnArrowKey(Key);
                continue;
            }
            {
                char C = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
                if (C >= '0' && C <= '9') {
                    SettingsUiOnDigit(C);
                }
            }
            continue;
        }

        /* Files：导航 / 写操作快捷键 / 确认与输入 */
        if (FilesUiIsFocused()) {
            if (Key == HID_KEY_ESCAPE) {
                FilesUiOnEscape();
                continue;
            }
            if (Key == HID_KEY_ENTER) {
                FilesUiOnEnter();
                continue;
            }
            if (Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
                FilesUiOnArrow(Key == HID_KEY_DOWN);
                continue;
            }
            if (Key == HID_KEY_BACKSPACE) {
                FilesUiOnBackspace();
                continue;
            }
            if (Key == HID_KEY_DELETE) {
                FilesUiOnDeleteKey();
                continue;
            }
            if (Key == HID_KEY_CAPSLOCK) {
                HIDKeyboardToggleCapsLock();
                HalKeyboardSetLeds(HIDKeyboardGetLeds());
                continue;
            }
            {
                char C = HIDKeyCodeToASCII(Key, Report->ModifierKeys);
                if (C != 0) {
                    FilesUiOnChar(C);
                }
            }
            continue;
        }

        if (Key == HID_KEY_ENTER) {
            /* ConsoleOnEnter → EnsureShell：空桌面时开 Shell */
            ConsoleOnEnter();
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
        if (Key == HID_KEY_C &&
            (Report->ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL))) {
            ShellOnInterrupt();
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
    HAL_KEYBOARD_REPORT Report = {0};
    HAL_KEYBOARD_REPORT Previous = {0};
    DebugWrite("shell task running (preemptive)\n");
    for (;;) {
        while (HalSerialDataReady()) {
            char C = HalSerialReadChar();
            if (SettingsUiIsFocused()) {
                if (C == 0x1B) {
                    SettingsUiOnEscape();
                } else if (C >= '0' && C <= '9') {
                    SettingsUiOnDigit(C);
                }
                continue;
            }
            if (FilesUiIsFocused()) {
                if (C == 0x1B) {
                    FilesUiOnEscape();
                } else if (C == '\r' || C == '\n') {
                    FilesUiOnEnter();
                } else if (C == '\b' || C == 127) {
                    FilesUiOnBackspace();
                } else if (C >= 32 && C <= 126) {
                    FilesUiOnChar(C);
                }
                continue;
            }
            if (C == '\r' || C == '\n') {
                ConsoleOnEnter();
            } else if (C == 3) {
                ShellOnInterrupt();
            } else if (C == '\b' || C == 127) {
                ConsoleOnBackspace();
            } else if (C >= 32 && C <= 126) {
                ConsoleOnChar(C);
            }
        }
        HalCpuHalt();
        /* PR-V5 virt：无抢占，GuiTask 饿死；在 shell 循环里顺带刷鼠标 */
        if (HalPlatformVirtConsole()) {
            GuiPollMouse();
            HalVideoPresent();
        }
        while (HalKeyboardDequeue(&Report)) {
            FeedHid(&Report, &Previous);
            Previous = Report;
        }
        HalNetPoll();
#ifndef TOY_LWIP
        TcpPoll();
#else
        if (!LwIpActive()) {
            TcpPoll();
        }
        LwIpPoll();
#endif
        {
            UDP_DATAGRAM Dg;
            int (*RecvFn)(UDP_DATAGRAM *) = UdpRecv;
#ifdef TOY_LWIP
            if (LwIpActive()) {
                RecvFn = LwIpUdpRecv;
            }
#endif
            while (RecvFn(&Dg)) {
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
