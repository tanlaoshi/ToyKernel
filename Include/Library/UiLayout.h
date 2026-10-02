/*
 * UiLayout.h — PR-UI-layout-set：三分栏 / 行高 / 边距令牌（活文档 §4.2）
 *
 * 以 1280×720 @100% 钉死；其它分辨率仍走 ThemeUiScale，令牌数值先不缩放。
 * Settings 本刀先用；Files/Store/桌面后续刀抄同一头。
 */
#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include "BootTypes.h"
#include "Font.h"

#define UI_LAYOUT_PAD           12u
#define UI_LAYOUT_GAP           8u
#define UI_LAYOUT_SIDE_W        168u
#define UI_LAYOUT_DETAIL_MIN_W  220u
#define UI_LAYOUT_BAR_H         40u
#define UI_LAYOUT_BTN_W         96u
#define UI_LAYOUT_BTN_H         32u
#define UI_LAYOUT_ICON_GAP      32u
#define UI_LAYOUT_SEP_W         1u
#define UI_LAYOUT_SB_W          12u

/* 列表/导航行高：max(FontAdvanceY()+8, 28) */
static inline UINT32 UiLayoutRowH(void) {
    UINT32 H = FontAdvanceY() + 8u;
    return (H > 28u) ? H : 28u;
}

/*
 * 客户区宽 ClientW → 左栏 / 中列表 / 右详情。
 * 窄窗：无侧栏或无详情（SideW/DetailW 可为 0）。
 */
static inline void UiLayoutTriple(UINT32 ClientW, UINT32 *SideW, UINT32 *ListW,
                                  UINT32 *DetailW) {
    UINT32 Side;
    UINT32 Detail;
    UINT32 Rest;
    UINT32 MinDetail;

    Side = 0;
    Detail = 0;
    if (ClientW > UI_LAYOUT_SIDE_W + UI_LAYOUT_DETAIL_MIN_W + 80u) {
        Side = UI_LAYOUT_SIDE_W;
        MinDetail = UI_LAYOUT_DETAIL_MIN_W;
        if (ClientW / 3u > MinDetail) {
            MinDetail = ClientW / 3u;
        }
        Rest = ClientW - Side - UI_LAYOUT_SEP_W * 2u;
        if (Rest > MinDetail + 80u) {
            Detail = MinDetail;
            if (Detail + 80u > Rest) {
                Detail = Rest > 80u ? Rest - 80u : 0;
            }
        }
    } else if (ClientW > UI_LAYOUT_SIDE_W + 120u) {
        Side = UI_LAYOUT_SIDE_W;
    }
    if (SideW) {
        *SideW = Side;
    }
    if (DetailW) {
        *DetailW = Detail;
    }
    if (ListW) {
        *ListW = ClientW - Side - Detail;
        if (Side > 0 && *ListW >= UI_LAYOUT_SEP_W) {
            *ListW -= UI_LAYOUT_SEP_W;
        }
        if (Detail > 0 && *ListW >= UI_LAYOUT_SEP_W) {
            *ListW -= UI_LAYOUT_SEP_W;
        }
    }
}

#endif
