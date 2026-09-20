/*
 * TasksHid.c — HID 报告送进控制台（PR-S-tasks-1）
 */
#include "Tasks.h"
#include "TasksPrivate.h"
#include "Hal.h"
#include "HalVideo.h"
#include "HIDKeyboard.h"
#include "Console.h"
#include "Gui.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "EditUi.h"
#include "Udp.h"
#include "Tcp.h"
#include "LwIp.h"
#include "Debug.h"
#include "ShellCommands.h"
#include "ToySerialLog.h"

void FeedHid(HAL_KEYBOARD_REPORT *Report, HAL_KEYBOARD_REPORT *Previous) {
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

        /* Edit：文本编辑 / Ctrl+S 保存 */
        if (EditUiIsFocused()) {
            if (Key == HID_KEY_ESCAPE) {
                EditUiOnEscape();
                continue;
            }
            if (Key == HID_KEY_ENTER) {
                EditUiOnEnter();
                continue;
            }
            if (Key == HID_KEY_UP || Key == HID_KEY_DOWN) {
                EditUiOnArrow(Key == HID_KEY_DOWN);
                continue;
            }
            if (Key == HID_KEY_LEFT || Key == HID_KEY_RIGHT) {
                EditUiOnArrowLeftRight(Key == HID_KEY_RIGHT);
                continue;
            }
            if (Key == HID_KEY_BACKSPACE) {
                EditUiOnBackspace();
                continue;
            }
            if (Key == HID_KEY_DELETE) {
                EditUiOnDeleteKey();
                continue;
            }
            if (Key == HID_KEY_S &&
                (Report->ModifierKeys & (HID_MOD_LCTRL | HID_MOD_RCTRL))) {
                EditUiSave();
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
                    EditUiOnChar(C);
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

        /* 用户态窗：焦点路由 HID → Poll（300+code）；勿进 Shell 行缓冲 */
        if (GuiFocusKind() == GUI_WIN_USER) {
            if (Key == HID_KEY_CAPSLOCK) {
                HIDKeyboardToggleCapsLock();
                HalKeyboardSetLeds(HIDKeyboardGetLeds());
                continue;
            }
            GuiUserEnqueueKey(Key);
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
