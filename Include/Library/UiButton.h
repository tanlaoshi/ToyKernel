#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include "BootTypes.h"

/*
 * PR-GUI-btn-widget：按钮 widget 层。
 *
 * 目标：把"画按钮"与"判断命中 + 单击状态机"两件事从各页面里收口到一个
 * 小结构体，让 StoreUi/EditUi/DevicesUi/FilesUi 不再各自手维护 hit-rect 表
 * 与 press→release 状态机。绘制委托给底层 UiDrawButtonEx（NORMAL/HOVER/
 * PRESSED 三态）或 UiDrawButtonDisabled（DISABLED 态），本层不直接碰像素，
 * 以满足"不重写绘制"的硬约束。
 *
 * 状态机：OnClick 在按钮可见且启用时被页面 pump 调用，传入本次指针事件
 * (Pressed=按下, Released=松开)。内部维护 m_State：
 *   - NORMAL: 收到 Pressed 且命中 → PRESSED；否则不变。
 *   - PRESSED: 收到 Released 且命中 → 回 NORMAL 并返回 1（触发"单击"）；
 *              收到 Released 且未命中 → 回 NORMAL（取消）；其他不变。
 * 页面只在 Released 命中时才执行业务动作。
 */

typedef enum {
    UI_BUTTON_STATE_NORMAL = 0,
    UI_BUTTON_STATE_HOVER,
    UI_BUTTON_STATE_PRESSED,
    UI_BUTTON_STATE_DISABLED
} UI_BUTTON_STATE;

typedef struct {
    UINT32      X;
    UINT32      Y;
    UINT32      W;
    UINT32      H;
    const char *Text;
    int         Enabled;   /* 0=禁用，非 0=启用 */
    int         Visible;   /* 0=隐藏，非 0=可见 */
    /* 内部状态：页面应置零初始化，之后只通过 OnClick 改变。 */
    UI_BUTTON_STATE m_State;
} UI_BUTTON;

/* 按 widget 当前状态绘制。Visible=0 时直接返回。 */
void UiButtonDraw(const UI_BUTTON *Btn);

/* (Px,Py) 是否落在按钮矩形内（按钮不可见或禁用时返回 0）。 */
int UiButtonHit(const UI_BUTTON *Btn, UINT32 Px, UINT32 Py);

/*
 * 单击状态机。Pressed=1 表示按下事件，Pressed=0 表示松开事件；Hit=1 表示
 * 该事件坐标命中按钮。返回 1 当且仅当发生了一次"完整单击"（按下后松开
 * 且松开时仍在按钮上），页面据此执行动作。按钮禁用/隐藏时总是返回 0。
 */
int UiButtonOnClick(UI_BUTTON *Btn, int Pressed, int Hit);

#endif /* UI_BUTTON_H */
