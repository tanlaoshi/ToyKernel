#include "UiButton.h"
#include "UI.h"
#include "Theme.h"

/*
 * PR-GUI-btn-widget：widget 层实现。
 * 绘制委托给 UiDrawButtonEx（NORMAL/HOVER/PRESSED）或 UiDrawButtonDisabled
 * （DISABLED），本文件不直接画像素。颜色全部来自 Theme 按钮色 getter。
 */

void UiButtonDraw(const UI_BUTTON *Btn) {
    UINT32 Face;
    UINT32 TextColor;

    if (!Btn || !Btn->Visible) {
        return;
    }
    if (!Btn->Enabled) {
        /* 禁用：交给专用绘制路径，颜色已内置 muted。 */
        UiDrawButtonDisabled(Btn->X, Btn->Y, Btn->W, Btn->H, Btn->Text);
        return;
    }
    switch (Btn->m_State) {
        case UI_BUTTON_STATE_PRESSED:
            Face = ThemeButtonFacePressed();
            break;
        case UI_BUTTON_STATE_HOVER:
            Face = ThemeButtonFaceHover();
            break;
        default:
            Face = ThemeButtonFaceNormal();
            break;
    }
    TextColor = ThemeButtonTextNormal();
    UiDrawButtonEx(Btn->X, Btn->Y, Btn->W, Btn->H, Btn->Text,
                   TextColor, Face,
                   Btn->m_State == UI_BUTTON_STATE_HOVER,
                   Btn->m_State == UI_BUTTON_STATE_PRESSED);
}

int UiButtonHit(const UI_BUTTON *Btn, UINT32 Px, UINT32 Py) {
    if (!Btn || !Btn->Visible || !Btn->Enabled) {
        return 0;
    }
    return UiHitRect(Btn->X, Btn->Y, Btn->W, Btn->H, Px, Py);
}

int UiButtonOnClick(UI_BUTTON *Btn, int Pressed, int Hit) {
    if (!Btn || !Btn->Visible || !Btn->Enabled) {
        return 0;
    }
    if (Pressed) {
        if (Hit) {
            Btn->m_State = UI_BUTTON_STATE_PRESSED;
        }
        return 0;
    }
    /* Released */
    if (Btn->m_State != UI_BUTTON_STATE_PRESSED) {
        return 0;
    }
    Btn->m_State = UI_BUTTON_STATE_NORMAL;
    return Hit ? 1 : 0;
}
