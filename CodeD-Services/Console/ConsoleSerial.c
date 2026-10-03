/*
 * ConsoleSerial.c — 串口壳循环 + 单字符喂入（PR-BOX-1）
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "Hal.h"
#include "HIDKeyboard.h"
#include "ShellCommands.h"

void ConsoleSerialFeedChar(char C, int *SkipLf) {
    int Skip;

    if (ConsoleStdinUserHold()) {
        /* 仍回显：CoolTerm 常关 Local Echo；Hold 时静默像死机 */
        if (C >= 32 && C <= 126) {
            HalConsolePutChar(C);
        } else if (C == '\r' || C == '\n') {
            HalConsoleWriteSerial("\n");
        } else if (C == '\b' || C == 127) {
            HalConsoleBackspaceSerial();
        }
        ConsoleStdinPut(C);
        return;
    }
    Skip = (SkipLf != 0) ? *SkipLf : 0;
    if (ConsoleHistIgnoreRx()) {
        if (C == '\r') {
            if (SkipLf) {
                *SkipLf = 1;
            }
            ConsoleOnEnterEx(1);
            ConsoleHistClearIgnoreRx();
        } else if (C == '\n') {
            if (Skip) {
                if (SkipLf) {
                    *SkipLf = 0;
                }
            } else {
                ConsoleOnEnterEx(1);
                ConsoleHistClearIgnoreRx();
            }
        } else if (C == 3) {
            ConsoleHistClearIgnoreRx();
            ShellOnInterrupt();
        } else {
            (void)ConsoleHistFeedAnsi(C, 1);
        }
        return;
    }
    if (C == '\r') {
        if (SkipLf) {
            *SkipLf = 1;
        }
        ConsoleOnEnterEx(1);
        return;
    }
    if (C == '\n') {
        if (Skip) {
            if (SkipLf) {
                *SkipLf = 0;
            }
        } else {
            ConsoleOnEnterEx(1);
        }
        return;
    }
    if (SkipLf) {
        *SkipLf = 0;
    }
    if (C == '\b' || C == 127) {
        ConsoleOnBackspaceEx(1);
    } else if (C == 3) {
        ShellOnInterrupt();
    } else if (!ConsoleHistFeedAnsi(C, 1) && C >= 32 && C <= 126) {
        ConsoleOnCharEx(C, 1);
    }
}

/* PR-A9/V3：virt 串口 + virtio-input 键盘 */
void ConsoleSerialRun(void) {
    static HAL_KEYBOARD_REPORT Prev;
    HAL_KEYBOARD_REPORT Report;
    static int SkipLf;

    if (HalPlatformIsVirtSerialConsole()) {
        HalConsoleWriteSerial(
            "virt: serial shell (help/mem/ps/halt; kbd via virtio-input)\n");
    } else {
        HalConsoleWriteSerial("serial shell (help/mem/ps/halt)\n");
    }
    ConsoleAnnounceBootReady();
    for (;;) {
        HalCpuHalt();
        HalInputPoll();
        {
            int n = 0;
            while (HalSerialDataReady() && n < 256) {
                char C = HalSerialReadChar();
                n++;
                ConsoleSerialFeedChar(C, &SkipLf);
            }
        }
        while (HalKeyboardDequeue(&Report)) {
            int i;
            for (i = 0; i < 6; i++) {
                UINT8 Key = Report.KeyCode[i];
                int Was = 0;
                int j;
                if (Key == 0) {
                    continue;
                }
                for (j = 0; j < 6; j++) {
                    if (Prev.KeyCode[j] == Key) {
                        Was = 1;
                        break;
                    }
                }
                if (Was) {
                    continue;
                }
                if (Key == 0x28) {
                    ConsoleOnEnter();
                } else if (Key == 0x2A) {
                    ConsoleOnBackspace();
                } else if (Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
                    ConsoleOnHistHid(Key, 0);
                } else if (Key == HID_KEY_C &&
                           (Report.ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL)) != 0) {
                    ShellOnInterrupt();
                } else {
                    char C = HIDKeyCodeToASCII(Key, Report.ModifierKeys);
                    if (C) {
                        ConsoleOnChar(C);
                    }
                }
            }
            Prev = Report;
        }
        HalCpuRelax();
    }
}
