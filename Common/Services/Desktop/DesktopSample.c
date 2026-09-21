/*
 * DesktopSample.c — 桌面矩形重画与像素采样
 * 核心：Desktop.c
 */
#include "DesktopPrivate.h"

void DesktopDrawRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    int i;
    UINT32 Ix;
    UINT32 Iy;
    UINT32 Iw;
    UINT32 Ih;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        IconBounds(&gIcons[i], &Ix, &Iy, &Iw, &Ih);
        if (RectsOverlap(X, Y, W, H, Ix, Iy, Iw, Ih)) {
            DrawOneIconOccluded(&gIcons[i], i == gDeskSelected);
        }
    }
    TaskbarGeom(&BarY, &Sw, &Sh);
    if (RectsOverlap(X, Y, W, H, 0, BarY, Sw, TASKBAR_H)) {
        DrawTaskbarOccluded();
    } else if (gMenuOpen) {
        MenuGeom(&Mx, &My, &Mw, &Mh);
        if (RectsOverlap(X, Y, W, H, Mx, My, Mw, Mh)) {
            DrawStartMenuRaw();
        } else if (gMenuAppsOpen || gMenuGameOpen) {
            UINT32 Fx;
            UINT32 Fy;
            UINT32 Fw;
            UINT32 Fh;

            if (gMenuAppsOpen) {
                AppsFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
            } else {
                GameFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
            }
            if (RectsOverlap(X, Y, W, H, Fx, Fy, Fw, Fh)) {
                DrawStartMenuRaw();
            }
        }
    }
}

int DesktopSamplePixel(UINT32 X, UINT32 Y, UINT32 *Out) {
    int i;
    UINT32 Border;
    UINT32 LabelW;
    UINT32 LabelX;
    UINT32 LabelY;
    const char *P;
    UINT32 Cx;
    UINT32 CellH;
    const FONT_FACE *Face;
    UINT32 Sw;
    UINT32 Sh;
    UINT32 BarY;
    UINT32 Mx;
    UINT32 My;
    UINT32 Mw;
    UINT32 Mh;

    if (!Out) {
        return 0;
    }
    CellH = FontCellH();
    Face = FontGetCurrent();

    TaskbarGeom(&BarY, &Sw, &Sh);
    if (gMenuOpen) {
        MenuGeom(&Mx, &My, &Mw, &Mh);
        if (X >= Mx && Y >= My && X < Mx + Mw && Y < My + Mh) {
            *Out = ThemeControlFace();
            return 1;
        }
        if (gMenuAppsOpen || gMenuGameOpen) {
            UINT32 Fx;
            UINT32 Fy;
            UINT32 Fw;
            UINT32 Fh;

            if (gMenuAppsOpen) {
                AppsFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
            } else {
                GameFlyoutGeom(&Fx, &Fy, &Fw, &Fh);
            }
            if (X >= Fx && Y >= Fy && X < Fx + Fw && Y < Fy + Fh) {
                *Out = ThemeControlFace();
                return 1;
            }
        }
    }
    if (Y >= BarY && Y < Sh) {
        UINT32 Bx;
        UINT32 By;
        UINT32 Bw;
        UINT32 Bh;

        StartBtnGeom(&Bx, &By, &Bw, &Bh);
        if (X >= Bx && X < Bx + Bw &&
            Y >= By && Y < By + Bh) {
            *Out = gMenuOpen ? ThemeTaskbarButtonActive() : ThemeTaskbarButton();
            return 1;
        }
        *Out = ThemeTaskbarBackground();
        return 1;
    }

    for (i = 0; i < DESKTOP_ICON_COUNT; i++) {
        const DESKTOP_ICON *Icon = &gIcons[i];
        int Selected = (i == gDeskSelected);
        UINT32 Ix;
        UINT32 Iy;
        UINT32 Iw;
        UINT32 Ih;

        IconBounds(Icon, &Ix, &Iy, &Iw, &Ih);
        if (X < Ix || Y < Iy || X >= Ix + Iw || Y >= Iy + Ih) {
            continue;
        }

        if (X >= Icon->X && Y >= Icon->Y &&
            X < Icon->X + DESKTOP_ICON_SIZE &&
            Y < Icon->Y + DESKTOP_ICON_SIZE) {
            Border = Selected ? ThemeIconSelect() : ThemeIconBorder();
            if (X == Icon->X || Y == Icon->Y ||
                X == Icon->X + DESKTOP_ICON_SIZE - 1 ||
                Y == Icon->Y + DESKTOP_ICON_SIZE - 1) {
                *Out = Border;
                return 1;
            }
            if (Selected &&
                (X == Icon->X + 1 || Y == Icon->Y + 1 ||
                 X == Icon->X + DESKTOP_ICON_SIZE - 2 ||
                 Y == Icon->Y + DESKTOP_ICON_SIZE - 2)) {
                *Out = ThemeIconSelect();
                return 1;
            }
            if (Icon->BmpReady && Icon->Bmp.Pixels) {
                UINT32 RelX = X - Icon->X;
                UINT32 RelY = Y - Icon->Y;
                if (RelX < Icon->Bmp.Width && RelY < Icon->Bmp.Height) {
                    *Out = Icon->Bmp.Pixels[RelY * Icon->Bmp.Width + RelX];
                    return 1;
                }
            }
            *Out = Icon->IconColor;
            return 1;
        }

        LabelW = Icon->Label ? FontStringWidth(Icon->Label) : 0;
        LabelX = Icon->X;
        if (LabelW < DESKTOP_ICON_SIZE) {
            LabelX = Icon->X + (DESKTOP_ICON_SIZE - LabelW) / 2;
        }
        LabelY = Icon->Y + DESKTOP_ICON_SIZE + DESKTOP_LABEL_PAD;
        if (!Icon->Label || Face == 0 || Y < LabelY || Y >= LabelY + CellH) {
            continue;
        }
        P = Icon->Label;
        Cx = LabelX;
        while (P && *P) {
            UINT32 Cp;
            UINTN N;
            UINT32 Adv;
            UINT32 Gw;
            UINT32 Gh;
            const UINT8 *Glyph;

            N = Utf8Decode(P, &Cp);
            if (N == 0) {
                P++;
                continue;
            }
            Adv = FontCodepointAdvance(Cp);
            if (Cp != '\n' && X >= Cx && X < Cx + Adv) {
                UINT32 RelX;
                UINT32 RelY;
                UINT32 Gx;
                UINT32 Gy;

                Glyph = FontGlyphCp(Cp, &Gw, &Gh);
                RelX = X - Cx;
                RelY = Y - LabelY;
                if (Glyph != 0) {
                    UINT32 Bpr = (Gw + 7) / 8;
                    UINT32 Stretch = FontGlyphStretch(Gh);
                    UINT32 DrawnH;
                    UINT32 OffY = 0;

                    if (Stretch < 1) {
                        Stretch = 1;
                    }
                    DrawnH = Gh * Stretch;
                    if (CellH > DrawnH) {
                        OffY = (CellH - DrawnH) / 2;
                    }
                    if (RelY >= OffY) {
                        Gx = RelX / Stretch;
                        Gy = (RelY - OffY) / Stretch;
                        if (Gx < Gw && Gy < Gh) {
                            UINT8 Byte = Glyph[Gy * Bpr + (Gx / 8)];
                            int Bit = 7 - (int)(Gx % 8);

                            if (Byte & (1 << Bit)) {
                                *Out = Selected ? ThemeIconSelect() : ThemeIconText();
                                return 1;
                            }
                        }
                    }
                }
                return 0;
            }
            Cx += Adv;
            P += N;
        }
    }
    return 0;
}
