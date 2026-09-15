/*
 * FilesUiPaint.c — Files 绘制（PR-S-filesui-split-1）
 *
 * 从 FilesUi.c 迁出 Paint*；只搬家、不改逻辑。
 */
#include "FilesUiPriv.h"

void PaintOverlay(const char *Line1, const char *Line2, const char *Line3) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 BoxX;
    UINT32 BoxY;
    UINT32 BoxW;
    UINT32 BoxH;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }
    BoxW = W > 40 ? W - 40 : W;
    BoxH = LineH * 5 + 24;
    if (BoxH > H - 20) {
        BoxH = H > 20 ? H - 20 : H;
    }
    BoxX = X + (W - BoxW) / 2;
    BoxY = Y + (H - BoxH) / 2;

    GuiFrameBufferBegin();
    HalVideoFillRect(BoxX, BoxY, BoxW, BoxH, COLOR_LIGHT_GRAY);
    /* PR-G11：确认/输入框边框走 UiDrawRectangle */
    UiDrawRectangle(BoxX, BoxY, BoxW, BoxH, COLOR_BLACK);
    if (BoxW > 4 && BoxH > 4) {
        UiDrawRectangle(BoxX + 1, BoxY + 1, BoxW - 2, BoxH - 2, COLOR_DARK_GRAY);
    }
    HalVideoSetClipRegion(BoxX + 4, BoxY + 4, BoxW > 8 ? BoxW - 8 : BoxW, BoxH > 8 ? BoxH - 8 : BoxH,
                          COLOR_LIGHT_GRAY);
    DrawLine(BoxX + 12, BoxY + 12, Line1 ? Line1 : "", COLOR_BLACK);
    if (Line2) {
        DrawLine(BoxX + 12, BoxY + 12 + LineH, Line2, COLOR_BLACK);
    }
    if (Line3) {
        DrawLine(BoxX + 12, BoxY + 12 + LineH * 2, Line3, COLOR_DARK_GRAY);
    }
    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

void PaintList(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 RowY;
    UINT32 SideW;
    UINT32 Cx;
    UINT32 Cw;
    int Visible;
    int i;
    char Line[96];
    char PathShow[FILES_PATH_MAX + 8];

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    GuiFrameBufferBegin();
    HalVideoFillRect(X, Y, W, H, Bg);
    HalVideoSetClipRegion(X, Y, W, H, Bg);

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }

    /* PR-U1/U2：左栏固定宽 + 书签；窄窗退回单栏 */
    SideW = 0;
    gSideW = 0;
    if (W > FILES_SIDE_W + 160u) {
        SideW = FILES_SIDE_W;
    }
    gContentX = X + SideW;
    gContentW = W - SideW;
    Cx = gContentX;
    Cw = gContentW;

    if (SideW > 0) {
        UINT32 RowW;

        gSideX = X;
        gSideY = Y;
        gSideW = SideW;
        gSideLineH = LineH;
        gSideRow0 = Y + 8 + LineH + 4;
        RowW = SideW > 10 ? SideW - 10 : SideW;

        HalVideoFillRect(X, Y, SideW, H, FILES_SIDE_BG);
        if (SideW > 3) {
            HalVideoFillRect(X + SideW - 3, Y, 3, H, COLOR_DARK_GRAY);
        }
        DrawLine(X + 8, Y + 8, "Places", COLOR_BLACK);
        for (i = 0; i < FILES_BOOKMARK_COUNT; i++) {
            UiDrawListRow(X + 4, gSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          gBookmarks[i].Label,
                          i == gSideSel, i == gSideHover);
        }
    }

    PathShow[0] = 0;
    CopyStr(PathShow, sizeof(PathShow), "Path: ");
    {
        int n = 0;
        while (PathShow[n]) {
            n++;
        }
        if (gCwd[0]) {
            CopyStr(PathShow + n, (int)sizeof(PathShow) - n, gCwd);
        } else {
            CopyStr(PathShow + n, (int)sizeof(PathShow) - n, "/");
        }
    }
    DrawLine(Cx + 8, Y + 8, PathShow, COLOR_BLACK);
    DrawLine(Cx + 8, Y + 8 + LineH,
             "Enter open  d/Del rm  n mkdir  f file  r rename", COLOR_DARK_GRAY);
    if (gStatus[0]) {
        DrawLine(Cx + 8, Y + 8 + LineH * 2, gStatus, COLOR_BLUE);
    }

    /* PR-U3：内容区再分 列表 | 预览（窄则只列表） */
    gPrevW = 0;
    gPrevX = Cx;
    if (Cw > 360u) {
        gPrevW = Cw * 2u / 5u;
        if (gPrevW < 160u) {
            gPrevW = 160u;
        }
        if (gPrevW + 120u > Cw) {
            gPrevW = Cw > 120u ? Cw - 120u : 0;
        }
    }
    {
        UINT32 ListW = Cw - gPrevW;
        UINT32 ListX = Cx;

        Visible = 0;
        if (H > 8 + LineH * 4) {
            Visible = (int)((H - 8 - LineH * 4) / LineH);
        }
        if (Visible < 1) {
            Visible = 1;
        }
        if (gSelected < gScroll) {
            gScroll = gSelected;
        }
        if (gSelected >= gScroll + Visible) {
            gScroll = gSelected - Visible + 1;
        }
        if (gScroll < 0) {
            gScroll = 0;
        }

        gListVisible = Visible;
        gListTop = Y + 8 + LineH * 3 + 4;
        gListLineH = LineH;
        gSbVisible = (gCount > Visible) ? 1 : 0;
        gSbW = FILES_SB_W;
        gSbH = (UINT32)Visible * LineH;
        if (gSbH + gListTop > Y + H) {
            gSbH = (Y + H > gListTop) ? (Y + H - gListTop) : 0;
        }
        gSbX = (ListW > FILES_SB_W + 8) ? (ListX + ListW - FILES_SB_W - 4) : (ListX + 4);
        if (gPrevW > 0 && gSbX + gSbW > ListX + ListW) {
            gSbX = ListX + 4;
        }
        gSbY = gListTop;
        gListRowW = ListW > 8 ? ListW - 8 : ListW;
        if (gSbVisible && gListRowW > FILES_SB_W + 8) {
            gListRowW -= (FILES_SB_W + 4);
        }

        RowY = gListTop;
        for (i = 0; i < Visible && gScroll + i < gCount; i++) {
            const FAT_DIRECTORY_ENTRY *E = &gEnts[gScroll + i];
            int Idx = gScroll + i;
            int k = 0;
            int j;

            if (E->Attr & FAT_ATTR_DIR) {
                Line[k++] = '[';
                Line[k++] = 'D';
                Line[k++] = ']';
                Line[k++] = ' ';
            } else {
                Line[k++] = ' ';
                Line[k++] = ' ';
                Line[k++] = ' ';
                Line[k++] = ' ';
            }
            for (j = 0; E->Name[j] && k < (int)sizeof(Line) - 1; j++) {
                Line[k++] = E->Name[j];
            }
            Line[k] = 0;
            UiDrawListRow(ListX + 4, RowY, gListRowW, LineH, Line,
                          Idx == gSelected, Idx == gHoverIdx);
            RowY += LineH;
        }
        if (gSbVisible && gSbH > 0) {
            UiDrawScrollBar(gSbX, gSbY, gSbW, gSbH, gScroll, Visible, gCount);
        }
        if (gCount == 0) {
            UINT32 BoxX = ListX + 12;
            UINT32 BoxY = gListTop;
            UINT32 BoxW = ListW > 24 ? ListW - 24 : ListW;
            UINT32 BoxH = LineH * 4 + 20;
            UINT32 Remain;

            if (BoxY + 8 < Y + H) {
                Remain = (Y + H) - BoxY - 8;
                if (BoxH > Remain) {
                    BoxH = Remain;
                }
            }
            if (BoxW > 8 && BoxH > LineH + 8) {
                UiFillRectangle(BoxX, BoxY, BoxW, BoxH, COLOR_LIGHT_GRAY);
                UiDrawRectangle(BoxX, BoxY, BoxW, BoxH, COLOR_DARK_GRAY);
                DrawLine(BoxX + 12, BoxY + LineH, LocStr(MSG_FILES_EMPTY), COLOR_DARK_GRAY);
                if (BoxH >= LineH * 3) {
                    DrawLine(BoxX + 12, BoxY + LineH * 2 + 4,
                             LocStr(MSG_FILES_EMPTY_HINT), COLOR_GRAY);
                }
            }
        }

        if (gPrevW > 0) {
            UINT32 Px = Cx + ListW;
            UINT32 Py = Y + 8;
            UINT32 Pw = gPrevW;
            UINT32 InnerX;
            UINT32 InnerY;
            UINT32 InnerW;
            UINT32 InnerH;
            UINT32 CurY;
            UINTN ti;

            gPrevX = Px;
            HalVideoFillRect(Px, Y, 2, H, COLOR_DARK_GRAY);
            HalVideoFillRect(Px + 2, Y, Pw > 2 ? Pw - 2 : Pw, H, 0x00D8D8E0u);
            DrawLine(Px + 10, Py, "Preview", COLOR_BLACK);
            DrawLine(Px + 10, Py + LineH, gViewTitle[0] ? gViewTitle : "(none)",
                     COLOR_DARK_GRAY);

            InnerX = Px + 8;
            InnerY = Py + LineH * 2 + 8;
            InnerW = Pw > 16 ? Pw - 16 : Pw;
            InnerH = (Y + H > InnerY + 8) ? (Y + H - InnerY - 8) : 0;

            if (gPrevKind == PREV_EMPTY || gPrevKind == PREV_NONE) {
                if (InnerW > 8 && InnerH > LineH + 8) {
                    UiFillRectangle(InnerX, InnerY, InnerW, InnerH > LineH * 4 ? LineH * 4 : InnerH,
                                    COLOR_LIGHT_GRAY);
                    UiDrawRectangle(InnerX, InnerY,
                                    InnerW, InnerH > LineH * 4 ? LineH * 4 : InnerH,
                                    COLOR_DARK_GRAY);
                    DrawLine(InnerX + 10, InnerY + LineH,
                             LocStr(MSG_FILES_EMPTY), COLOR_DARK_GRAY);
                }
            } else if (gPrevKind == PREV_DIR) {
                DrawLine(InnerX, InnerY, "[Directory]", COLOR_BLUE);
                DrawLine(InnerX, InnerY + LineH, "Enter to open", COLOR_DARK_GRAY);
            } else if (gPrevKind == PREV_ELF) {
                DrawLine(InnerX, InnerY, "ELF executable", COLOR_BLUE);
                DrawLine(InnerX, InnerY + LineH, "Enter to run", COLOR_DARK_GRAY);
            } else if (gPrevKind == PREV_ERR) {
                DrawLine(InnerX, InnerY, "Cannot read file", COLOR_BLUE);
            } else if (gPrevKind == PREV_BIN) {
                DrawLine(InnerX, InnerY, "Binary file", COLOR_BLUE);
                DrawLine(InnerX, InnerY + LineH, "Enter = hex-ish view", COLOR_DARK_GRAY);
            } else if (gPrevKind == PREV_TEXT && InnerH > LineH) {
                char Row[72];
                int Col = 0;
                UINT32 MaxCols = (InnerW > 8) ? (InnerW - 4) / (FontAdvanceX() ? FontAdvanceX() : 8) : 20;

                if (MaxCols > sizeof(Row) - 1) {
                    MaxCols = sizeof(Row) - 1;
                }
                if (MaxCols < 8) {
                    MaxCols = 8;
                }
                CurY = InnerY;
                for (ti = 0; ti < gViewLen && CurY + LineH <= InnerY + InnerH; ti++) {
                    char C = gView[ti];
                    if (C == '\n' || Col >= (int)MaxCols) {
                        Row[Col] = 0;
                        DrawLine(InnerX, CurY, Row, COLOR_BLACK);
                        CurY += LineH;
                        Col = 0;
                        if (C == '\n') {
                            continue;
                        }
                    }
                    if (C == '\r') {
                        continue;
                    }
                    if (C >= 32 && C < 127) {
                        Row[Col++] = C;
                    } else {
                        Row[Col++] = '.';
                    }
                }
                if (Col > 0 && CurY + LineH <= InnerY + InnerH) {
                    Row[Col] = 0;
                    DrawLine(InnerX, CurY, Row, COLOR_BLACK);
                }
            }
        }
    }

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

void PaintView(void) {
    UINT32 X;
    UINT32 Y;
    UINT32 W;
    UINT32 H;
    UINT32 Bg;
    UINT32 LineH;
    UINT32 CurY;
    UINTN i;
    char Title[80];
    int ti;

    if (!GuiFocusClient(&X, &Y, &W, &H, &Bg)) {
        return;
    }
    GuiFrameBufferBegin();
    HalVideoFillRect(X, Y, W, H, Bg);
    HalVideoSetClipRegion(X, Y, W, H, Bg);

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }

    Title[0] = 0;
    CopyStr(Title, sizeof(Title), LocStr(MSG_FILES_VIEW));
    ti = 0;
    while (Title[ti]) {
        ti++;
    }
    CopyStr(Title + ti, (int)sizeof(Title) - ti, gViewTitle);
    DrawLine(X + 8, Y + 8, Title, COLOR_BLACK);
    DrawLine(X + 8, Y + 8 + LineH, "Esc = back to list", COLOR_DARK_GRAY);

    CurY = Y + 8 + LineH * 2 + 4;
    {
        char Row[96];
        int Col = 0;
        UINT32 MaxCols = (W > 16) ? (W - 16) / (FontAdvanceX() ? FontAdvanceX() : 8) : 40;

        if (MaxCols > sizeof(Row) - 1) {
            MaxCols = sizeof(Row) - 1;
        }
        if (MaxCols < 8) {
            MaxCols = 8;
        }
        for (i = 0; i < gViewLen && CurY + LineH <= Y + H; i++) {
            char C = gView[i];
            if (C == '\n' || Col >= (int)MaxCols) {
                Row[Col] = 0;
                DrawLine(X + 8, CurY, Row, COLOR_BLACK);
                CurY += LineH;
                Col = 0;
                if (C == '\n') {
                    continue;
                }
            }
            if (C == '\r') {
                continue;
            }
            if (C >= 32 && C < 127) {
                Row[Col++] = C;
            } else {
                Row[Col++] = '.';
            }
        }
        if (Col > 0 && CurY + LineH <= Y + H) {
            Row[Col] = 0;
            DrawLine(X + 8, CurY, Row, COLOR_BLACK);
        }
    }

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}

void PaintConfirm(void) {
    char Line2[80];
    const char *Name = "?";

    if (gSelected >= 0 && gSelected < gCount) {
        Name = gEnts[gSelected].Name;
    }
    CopyStr(Line2, sizeof(Line2), "Delete ");
    {
        int n = 0;
        while (Line2[n]) {
            n++;
        }
        CopyStr(Line2 + n, (int)sizeof(Line2) - n, Name);
        n = 0;
        while (Line2[n]) {
            n++;
        }
        CopyStr(Line2 + n, (int)sizeof(Line2) - n, " ?");
    }
    PaintList();
    PaintOverlay("Confirm delete", Line2, "Y = yes   N/Esc = cancel");
}

void PaintPrompt(void) {
    char Title[40];
    char Line2[FILES_NAME_MAX + 8];
    int i;

    if (gPromptKind == FILES_PROMPT_MKDIR) {
        CopyStr(Title, sizeof(Title), LocStr(MSG_FILES_NEW_DIR));
    } else if (gPromptKind == FILES_PROMPT_NEWFILE) {
        CopyStr(Title, sizeof(Title), LocStr(MSG_FILES_NEW_FILE));
    } else {
        CopyStr(Title, sizeof(Title), LocStr(MSG_FILES_RENAME));
    }
    Line2[0] = '>';
    Line2[1] = ' ';
    for (i = 0; i < gPromptLen && i < FILES_NAME_MAX - 1; i++) {
        Line2[2 + i] = gPrompt[i];
    }
    Line2[2 + i] = '_';
    Line2[3 + i] = 0;
    PaintList();
    PaintOverlay(Title, Line2, LocStr(MSG_FILES_PROMPT_HINT));
}

void Paint(void) {
    if (!FocusFilesWindow()) {
        return;
    }
    if (gMode == FILES_MODE_VIEW) {
        PaintView();
    } else if (gMode == FILES_MODE_CONFIRM) {
        PaintConfirm();
    } else if (gMode == FILES_MODE_PROMPT) {
        PaintPrompt();
    } else {
        PaintList();
    }
}

