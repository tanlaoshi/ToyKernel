/*
 * ThemeModern.c — PR-UI-palette：现代浅色板（出厂默认）
 *
 * 色值钉死见 Documents/开发/UI颜值与布局.md §3.2。
 */
#include "ThemePrivate.h"

#define M_DESKTOP   0x00E6E8ECu
#define M_SHELL     0x00F4F1EAu
#define M_SETTINGS  0x00F7F8FAu
#define M_TITLE_F   0x002C3038u
#define M_TITLE_I   0x005C616Au
#define M_TITLE_H   0x003A4050u
#define M_BORDER_F  0x003A4050u
#define M_BORDER_I  0x00C5CAD3u
#define M_BORDER_H  0x003D5A80u
#define M_TITLE_TX  0x00F4F5F7u
#define M_CLOSE     0x00C0423Au
#define M_TASKBAR   0x00DDE1E7u
#define M_TB_BTN    0x00F7F8FAu
#define M_TB_ACT    0x002C3038u
#define M_CTRL_FACE 0x00FFFFFFu
#define M_CTRL_BRD  0x00C5CAD3u
#define M_ACCENT    0x003D5A80u
#define M_SHADOW    0x001C1E22u
#define M_TEXT      0x001C1E22u
#define M_MUTED     0x005C616Au
#define M_SEP       0x00D0D5DDu
#define M_LIST_SEL  0x00D7E2EEu
#define M_ON_ACC    0x00F7F8FAu
#define M_PANEL_S   0x00F7F8FAu
#define M_PANEL_D   0x00F7F8FAu
#define M_SCROLL_T  0x00E6E8ECu
#define M_SCROLL_B  0x00C5CAD3u
#define M_SCROLL_H  0x003D5A80u
#define M_DIALOG    0x00FFFFFFu

void ThemeModernApplyColors(void) {
    gDesktopBg = M_DESKTOP;
    gShellClientBg = M_SHELL;
}

void ThemeModernApplyDefaults(void) {
    ThemeModernApplyColors();
    gWallpaper = 1;
    gDesktopGrad = 0;
}

UINT32 ThemeModernSettingsClientBackground(void) { return M_SETTINGS; }
UINT32 ThemeModernWindowTitleFocus(void) { return M_TITLE_F; }
UINT32 ThemeModernWindowTitleIdle(void) { return M_TITLE_I; }
UINT32 ThemeModernWindowTitleHover(void) { return M_TITLE_H; }
UINT32 ThemeModernWindowBorderFocus(void) { return M_BORDER_F; }
UINT32 ThemeModernWindowBorderIdle(void) { return M_BORDER_I; }
UINT32 ThemeModernWindowBorderHover(void) { return M_BORDER_H; }
UINT32 ThemeModernWindowTitleText(void) { return M_TITLE_TX; }
UINT32 ThemeModernCloseButton(void) { return M_CLOSE; }
UINT32 ThemeModernTaskbarBackground(void) { return M_TASKBAR; }
UINT32 ThemeModernTaskbarButton(void) { return M_TB_BTN; }
UINT32 ThemeModernTaskbarButtonActive(void) { return M_TB_ACT; }
UINT32 ThemeModernControlFace(void) { return M_CTRL_FACE; }
UINT32 ThemeModernControlBorder(void) { return M_CTRL_BRD; }
UINT32 ThemeModernControlAccent(void) { return M_ACCENT; }
UINT32 ThemeModernWindowShadowColor(void) { return M_SHADOW; }

UINT32 ThemeModernButtonFaceNormal(void) { return M_CTRL_FACE; }
UINT32 ThemeModernButtonFaceHover(void) { return M_LIST_SEL; }
UINT32 ThemeModernButtonFacePressed(void) { return M_SEP; }
UINT32 ThemeModernButtonFaceDisabled(void) { return M_SCROLL_T; }
UINT32 ThemeModernButtonBorderNormal(void) { return M_CTRL_BRD; }
UINT32 ThemeModernButtonBorderHover(void) { return M_ACCENT; }
UINT32 ThemeModernButtonBorderPressed(void) { return M_ACCENT; }
UINT32 ThemeModernButtonBorderDisabled(void) { return M_SEP; }
UINT32 ThemeModernButtonTextDisabled(void) { return M_MUTED; }

UINT32 ThemeModernShellText(void) { return M_TEXT; }
UINT32 ThemeModernShellPrompt(void) { return M_ACCENT; }
UINT32 ThemeModernIconText(void) { return M_TEXT; }
UINT32 ThemeModernIconBorder(void) { return M_CTRL_BRD; }
UINT32 ThemeModernIconSelect(void) { return M_ACCENT; }
UINT32 ThemeModernClockText(void) { return M_TEXT; }
UINT32 ThemeModernStartButtonText(void) { return M_TEXT; }
UINT32 ThemeModernMenuBorder(void) { return M_CTRL_BRD; }
UINT32 ThemeModernMenuText(void) { return M_TEXT; }
UINT32 ThemeModernMenuSep(void) { return M_SEP; }
UINT32 ThemeModernText(void) { return M_TEXT; }
UINT32 ThemeModernTextMuted(void) { return M_MUTED; }
UINT32 ThemeModernTextAccent(void) { return M_ACCENT; }
UINT32 ThemeModernTextOnAccent(void) { return M_ON_ACC; }
UINT32 ThemeModernPanelSideBackground(void) { return M_PANEL_S; }
UINT32 ThemeModernPanelDetailBackground(void) { return M_PANEL_D; }
UINT32 ThemeModernPanelSeparator(void) { return M_SEP; }
UINT32 ThemeModernScrollTrack(void) { return M_SCROLL_T; }
UINT32 ThemeModernScrollBorder(void) { return M_SCROLL_B; }
UINT32 ThemeModernScrollThumb(void) { return M_SCROLL_H; }
UINT32 ThemeModernDialogFace(void) { return M_DIALOG; }
UINT32 ThemeModernDialogBorder(void) { return M_CTRL_BRD; }
UINT32 ThemeModernListSelect(void) { return M_LIST_SEL; }
