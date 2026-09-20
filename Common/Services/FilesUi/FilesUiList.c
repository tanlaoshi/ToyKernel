/*
 * FilesUiList.c — 文件列表绘制
 * 核心：FilesUi.c
 */
#include "FilesUiPrivate.h"

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

        HalVideoFillRect(X, Y, SideW, H, ThemePanelSideBackground());
        if (SideW > 3) {
            HalVideoFillRect(X + SideW - 3, Y, 3, H, ThemePanelSeparator());
        }
        DrawLine(X + 8, Y + 8, "Volumes", ThemeText());
        for (i = 0; i < gPlaceCount; i++) {
            UiDrawListRow(X + 4, gSideRow0 + (UINT32)i * LineH, RowW, LineH,
                          gPlaces[i].Label,
                          i == gSideSel, i == gSideHover);
        }
    }

    /* 先算预览宽，再画 Path/快捷键（避免右栏盖住 mkdir 等） */
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
        UINT32 HintMax;
        const char *Hint1 = "Enter open  d/Del delete";
        const char *Hint2 = "n mkdir  f newfile  r rename";

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
        HintMax = ListW > 16 ? ListW - 16 : ListW;
        (void)HintMax;
        DrawLine(Cx + 8, Y + 8, PathShow, ThemeText());
        DrawLine(Cx + 8, Y + 8 + LineH, Hint1, ThemeTextMuted());
        DrawLine(Cx + 8, Y + 8 + LineH * 2, Hint2, ThemeTextMuted());
        if (gStatus[0]) {
            DrawLine(Cx + 8, Y + 8 + LineH * 3, gStatus, ThemeTextAccent());
        }
    }

    /* PR-U3：内容区再分 列表 | 预览（窄则只列表） */
    {
        UINT32 ListW = Cw - gPrevW;
        UINT32 ListX = Cx;
        UINT32 HeadLines = gStatus[0] ? 4u : 3u;

        Visible = 0;
        if (H > 8 + LineH * (HeadLines + 1)) {
            Visible = (int)((H - 8 - LineH * (HeadLines + 1)) / LineH);
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
        gListTop = Y + 8 + LineH * HeadLines + 4;
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
                UiFillRectangle(BoxX, BoxY, BoxW, BoxH, ThemeDialogFace());
                UiDrawRectangle(BoxX, BoxY, BoxW, BoxH, ThemeDialogBorder());
                DrawLine(BoxX + 12, BoxY + LineH, LocStr(MSG_FILES_EMPTY), ThemeTextMuted());
                if (BoxH >= LineH * 3) {
                    DrawLine(BoxX + 12, BoxY + LineH * 2 + 4,
                             LocStr(MSG_FILES_EMPTY_HINT), ThemeTextMuted());
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
            HalVideoFillRect(Px, Y, 2, H, ThemePanelSeparator());
            HalVideoFillRect(Px + 2, Y, Pw > 2 ? Pw - 2 : Pw, H, ThemePanelDetailBackground());
            DrawLine(Px + 10, Py, "Preview", ThemeText());
            DrawLine(Px + 10, Py + LineH, gViewTitle[0] ? gViewTitle : "(none)",
                     ThemeTextMuted());

            InnerX = Px + 8;
            InnerY = Py + LineH * 2 + 8;
            InnerW = Pw > 16 ? Pw - 16 : Pw;
            InnerH = (Y + H > InnerY + 8) ? (Y + H - InnerY - 8) : 0;

            if (gPrevKind == PREV_EMPTY || gPrevKind == PREV_NONE) {
                if (InnerW > 8 && InnerH > LineH + 8) {
                    UiFillRectangle(InnerX, InnerY, InnerW, InnerH > LineH * 4 ? LineH * 4 : InnerH,
                                    ThemeDialogFace());
                    UiDrawRectangle(InnerX, InnerY,
                                    InnerW, InnerH > LineH * 4 ? LineH * 4 : InnerH,
                                    ThemePanelSeparator());
                    DrawLine(InnerX + 10, InnerY + LineH,
                             LocStr(MSG_FILES_EMPTY), ThemeTextMuted());
                }
            } else if (gPrevKind == PREV_DIR) {
                DrawLine(InnerX, InnerY, "[Directory]", ThemeTextAccent());
                DrawLine(InnerX, InnerY + LineH, "Enter to open", ThemeTextMuted());
            } else if (gPrevKind == PREV_ELF) {
                DrawLine(InnerX, InnerY, "ELF executable", ThemeTextAccent());
                DrawLine(InnerX, InnerY + LineH, "Enter to run", ThemeTextMuted());
            } else if (gPrevKind == PREV_ERR) {
                DrawLine(InnerX, InnerY, "Cannot read file", ThemeTextAccent());
            } else if (gPrevKind == PREV_BIN) {
                DrawLine(InnerX, InnerY, "Binary file", ThemeTextAccent());
                DrawLine(InnerX, InnerY + LineH, "Enter = hex-ish view", ThemeTextMuted());
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
                        DrawLine(InnerX, CurY, Row, ThemeText());
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
                    DrawLine(InnerX, CurY, Row, ThemeText());
                }
            }
        }
    }

    HalVideoClearClip();
    GuiBackupFocusWindow();
    GuiFrameBufferEnd();
}
