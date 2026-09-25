/*
 * ConsoleWrite.c — 串口与屏幕输出
 * 核心：Console.c
 */
#include "Console.h"
#include "ConsolePrivate.h"
#include "UI.h"
#include "Hal.h"
#include "Gui.h"
#include "Font.h"
#include "SettingsUi.h"
#include "Locale.h"
#include "HIDKeyboard.h"
#include "LibWrite.h"
#include "ShellCommands.h"

void ConsoleDrawString(const char *Text, UINT32 Color) {
    UINT32 X0;
    UINT32 Y0;
    UINT32 X1;
    UINT32 Y1;
    UINT32 L;
    UINT32 T;
    UINT32 R;
    UINT32 B;
    UINT32 Cx;
    UINT32 Cy;
    UINT32 Cw;
    UINT32 Ch;
    UINT32 Bg;

    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    ConsoleSbBarReapplyClip();
    HalConsoleGetTextCursor(&X0, &Y0);
    HalConsoleDrawString(Text, Color);
    HalConsoleGetTextCursor(&X1, &Y1);
    /*
     * 换行/滚屏后 X 回到行首：若仍用 (X0,X1) 轴对齐包围盒，
     * max(X0,X1)+Advance 只盖住行首约一字，Compose 会把同行其余字盖掉 →「吞字」。
     */
    if (Y1 != Y0 || X1 < X0) {
        if (GuiFocusClient(&Cx, &Cy, &Cw, &Ch, &Bg) && Cw > 0 && Ch > 0) {
            if (Y1 < Y0) {
                /* 滚屏：整客户区 */
                GuiBackupSyncRect(Cx, Cy, Cw, Ch);
            } else {
                T = Y0 < Y1 ? Y0 : Y1;
                B = (Y0 > Y1 ? Y0 : Y1) + FontAdvanceY();
                if (B > Cy + Ch) {
                    B = Cy + Ch;
                }
                if (T < Cy) {
                    T = Cy;
                }
                if (B > T) {
                    GuiBackupSyncRect(Cx, T, Cw, B - T);
                }
            }
        }
    } else {
        L = X0 < X1 ? X0 : X1;
        T = Y0;
        R = (X0 > X1 ? X0 : X1) + FontAdvanceX();
        B = Y0 + FontAdvanceY();
        if (R > L && B > T) {
            GuiBackupSyncRect(L, T, R - L, B - T);
        }
    }
    GuiFocusSyncCursor();
    GuiFrameBufferEnd();
}

void ConsoleDrawChar(char C, UINT32 Color) {
    UINT32 X;
    UINT32 Y;

    GuiFrameBufferBegin();
    GuiFocusApplyClip();
    HalConsoleGetTextCursor(&X, &Y);
    HalConsoleDrawChar(C, Color);
    GuiBackupSyncRect(X, Y, FontCellW(), FontCellH());
    GuiFocusSyncCursor();
    /* PR-G-shell-present：打字回显合并 Present（ShellTask 轮询末刷） */
    GuiPresentShellEchoMark();
    GuiFrameBufferEnd();
}

/* 同时输出到串口与屏幕（帧缓冲未就绪时只写串口） */
void ConsoleWrite(const char *Text) {
    const char *P;

    if (Text == 0) {
        return;
    }
    HalConsoleWriteSerial(Text);
    for (P = Text; *P; P++) {
        gAtLineStart = (*P == '\n');
    }
    if (!HalConsoleVideoReady()) {
        return;
    }
    /*
     * 仅 Shell 客户区接受控制台绘制。焦点在 USER/Settings/Files 时若仍画 FB，
     * 会与 GuiDemo 标签等叠字，并污染用户窗备份（透视/花屏）。
     */
    if (GuiFocusKind() != GUI_WIN_SHELL) {
        int i;
        int HasShell;

        /*
         * 已有 Shell 窗时记入行缓冲，便于切回后滚轮看近期输出。
         * 尚无 Shell 时勿记（Arm64 自测 write("Hello EL0!") 会污染，
         * 开窗误走 sb-repaint、跳过欢迎语/toyos>）。
         */
        HasShell = 0;
        for (i = 0; i < GUI_MAX_WINS; i++) {
            if (GuiShellWindowActive(i)) {
                HasShell = 1;
                break;
            }
        }
        if (HasShell) {
            ConsoleSbFeed(Text);
        }
        return;
    }
    ConsoleSbEnsureLive();
    ConsoleSbFeed(Text);
    ConsoleDrawString(Text, ThemeShellText());
    ConsoleSbBarAfterWrite();
}

/* 按长度输出（SYS_WRITE 用；UTF-8 按码点画，不因中间的 NUL 截断） */
void ConsoleWriteLen(const char *Data, UINTN Len) {
    UINTN i;

    if (Data == 0 || Len == 0) {
        return;
    }
    i = 0;
    while (i < Len) {
        UINT32 Cp;
        UINTN N;
        char Tmp[5];
        UINTN k;

        if (Data[i] == '\n') {
            ConsoleWrite("\n");
            i++;
            continue;
        }
        if (Data[i] == '\0') {
            i++;
            continue;
        }
        /* 非法/截断 UTF-8：跳过单字节，避免死循环 */
        if (i + 4 > Len) {
            /* 剩余可能不足完整序列；尽量解，失败则跳过 */
        }
        N = Utf8Decode(Data + i, &Cp);
        if (N == 0 || i + N > Len) {
            i++;
            continue;
        }
        if (Cp < 32 || Cp == 127) {
            i += N;
            continue;
        }
        for (k = 0; k < N && k < sizeof(Tmp) - 1; k++) {
            Tmp[k] = Data[i + k];
        }
        Tmp[k] = 0;
        ConsoleWrite(Tmp);
        i += N;
    }
}

/* 输出 32 位十六进制 */
void ConsoleWriteHex32(UINT32 Value) {
    char Buf[12];
    HalSerialFormatHex(Buf, Value, 8);
    ConsoleWrite(Buf);
}

/* 输出 64 位十六进制 */
void ConsoleWriteHex64(UINT64 Value) {
    char Buf[20];
    HalSerialFormatHex(Buf, Value, 16);
    ConsoleWrite(Buf);
}
