#ifndef UI_ACTION_H
#define UI_ACTION_H

#include "UiButton.h"

/*
 * PR-GUI-btn-action：统一按钮事件分发。
 *
 * 页面声明「按钮 + 动作」；press→release 命中后由 UiActionDispatch 调 Fn。
 * SYNC：Fn 立即执行业务（EditUi Save 等）。
 * ASYNC：Fn 负责入队（如 StoreJobEnqueue）；本层不阻塞、不引入队列——
 * 「后台化」在 Fn 内完成。两型本层均立即调 Fn，Kind 供调用方语义区分。
 *
 * 本刀只交付分发层 + DEBUG 桩；不迁任何页面。
 */

typedef enum {
    UI_ACTION_SYNC = 0,
    UI_ACTION_ASYNC = 1
} UI_ACTION_KIND;

typedef void (*UI_ACTION_FN)(void *Ctx);

typedef struct {
    UI_BUTTON       Button;
    UI_ACTION_KIND  Kind;
    UI_ACTION_FN    Fn;
    void           *Ctx;
} UI_BUTTON_ACTION;

/*
 * 统一单击分发。Pressed/Hit 语义同 UiButtonOnClick。
 * 完整单击时调用 Fn(Ctx)（若非空）并返回 1；否则返回 0。
 * 按钮禁用/隐藏时总是返回 0。
 */
int UiActionDispatch(UI_BUTTON_ACTION *Act, int Pressed, int Hit);

/* DEBUG 桩：模拟 SYNC/ASYNC 各一次完整单击，串口打 uiaction: sync=/async= */
void UiActionSelfCheck(void);

#endif /* UI_ACTION_H */
