/*
 * GuiHit.c — 命中测试 / 叠放 Raise（PR-S-guiwm-split-1）
 *
 * 从 GuiWm.c 迁出；只搬家、不改逻辑。CloseWindow / 开窗见 GuiOpen.c。
 */
#include "GuiPriv.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Debug.h"
#include "SettingsUi.h"
#include "FilesUi.h"
#include "StoreUi.h"
#include "EditUi.h"
#include "ToySerialLog.h"

void WinCopy(GUI_WINDOW *Dst, const GUI_WINDOW *Src) {
    *Dst = *Src;
}

int PointInClose(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    UINT32 Bx;
    UINT32 By;
    UINT32 Bw;
    UINT32 Bh;

    if (!W->Active) {
        return 0;
    }
    CloseButtonRect(W, &Bx, &By, &Bw, &Bh);
    return X >= Bx && X < Bx + Bw && Y >= By && Y < By + Bh;
}

int PointInWindow(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return W->Active && X >= W->X && X < W->X + W->Width &&
           Y >= W->Y && Y < W->Y + W->Height;
}


int PointInTitle(const GUI_WINDOW *W, UINT32 X, UINT32 Y) {
    return W->Active && X >= W->X && X < W->X + W->Width &&
           Y >= W->Y && Y < W->Y + TITLE_HEIGHT;
}


int PointInAnyActiveWindow(UINT32 X, UINT32 Y) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && PointInWindow(&gWindows[i], X, Y)) {
            return 1;
        }
    }
    return 0;
}


int PointOnAnyClose(UINT32 X, UINT32 Y) {
    int i;

    for (i = 0; i < MAX_WINS; i++) {
        if (gWindows[i].Active && PointInClose(&gWindows[i], X, Y)) {
            return 1;
        }
    }
    return 0;
}

/* 将窗口移到最前（数组后部 = 绘制在上层） */
void RaiseWindow(int Idx) {
    int Top = Idx;
    int J;
    int HasBackup = 0;

    for (J = Idx + 1; J < MAX_WINS; J++) {
        if (gWindows[J].Active) {
            Top = J;
        }
    }
    if (Top == Idx) {
        gFocusWin = Idx;
        return;
    }
    for (J = 0; J < MAX_WINS; J++) {
        if (gWinBackupValid[J] || gWinBackup[J] != 0) {
            HasBackup = 1;
            break;
        }
    }
    {
        WinCopy(&gWinSwap, &gWindows[Idx]);
        for (J = Idx; J < Top; J++) {
            WinCopy(&gWindows[J], &gWindows[J + 1]);
        }
        WinCopy(&gWindows[Top], &gWinSwap);
        /* 窗口与备份必须一起挪，否则会把别的窗备份贴到错误位置（花屏） */
        if (HasBackup || gDragHasBackup) {
            ShiftWinBackupsUp(Idx, Top);
        }
        gFocusWin = Top;
    }
}


/* 置顶并按备份重合成，避免只改焦点却在下层写穿 */
void GuiRaiseToFront(int Idx) {
    if (Idx < 0 || Idx >= MAX_WINS || !gWindows[Idx].Active) {
        return;
    }
    RaiseWindow(Idx);
    SyncWindowVisuals();
    GuiFocusApply();
    /* 顶层无有效备份时补内容，再抓一份干净备份 */
    if (gWindows[Idx].Kind == GUI_WIN_SETTINGS) {
        SettingsUiRepaint();
        BackupWindowAt(Idx);
    } else if (gWindows[Idx].Kind == GUI_WIN_STORE) {
        StoreUiRepaint();
        BackupWindowAt(Idx);
    } else if (gWindows[Idx].Kind == GUI_WIN_FILES) {
        FilesUiRepaint();
        BackupWindowAt(Idx);
    } else if (gWindows[Idx].Kind == GUI_WIN_EDIT) {
        EditUiRepaint();
        BackupWindowAt(Idx);
    } else if (gWindows[Idx].Kind == GUI_WIN_USER) {
        DrawWindowAtEx(Idx, 0);
        PaintUserClient(Idx);
        BackupWindowAtEx(Idx, 1);
    } else if (gWindows[Idx].Kind == GUI_WIN_SHELL && !gWinBackupValid[Idx]) {
        /* 欢迎语级恢复；完整历史需备份一直有效 */
        GuiConsoleOpsOnShellOpened();
        BackupWindowAt(Idx);
    }
}


int GuiPointInAnyWindow(UINT32 X, UINT32 Y) {
    return PointInAnyActiveWindow(X, Y);
}
