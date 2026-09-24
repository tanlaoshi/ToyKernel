#include "UiAction.h"
#include "Debug.h"

/*
 * PR-GUI-btn-action：事件分发实现。
 * 单击状态机委托 UiButtonOnClick；命中后调 Fn(Ctx)。
 * SYNC/ASYNC 本层行为相同——异步入队由 Fn 自己完成。
 */

int UiActionDispatch(UI_BUTTON_ACTION *Act, int Pressed, int Hit) {
    if (!Act) {
        return 0;
    }
    if (!UiButtonOnClick(&Act->Button, Pressed, Hit)) {
        return 0;
    }
    if (Act->Fn) {
        Act->Fn(Act->Ctx);
    }
    (void)Act->Kind; /* 语义标记；本层不分支 */
    return 1;
}

/* ---- DEBUG 桩：证明 SYNC/ASYNC 完整单击均命中 Fn ---- */

static int gStubSyncHit;
static int gStubAsyncHit;

static void StubSync(void *Ctx) {
    (void)Ctx;
    gStubSyncHit = 1;
}

static void StubAsync(void *Ctx) {
    (void)Ctx;
    gStubAsyncHit = 1;
}

static void StubInit(UI_BUTTON_ACTION *Act, UI_ACTION_KIND Kind,
                     UI_ACTION_FN Fn) {
    Act->Button.X = 0;
    Act->Button.Y = 0;
    Act->Button.W = 40;
    Act->Button.H = 20;
    Act->Button.Text = (Kind == UI_ACTION_SYNC) ? "sync" : "async";
    Act->Button.Enabled = 1;
    Act->Button.Visible = 1;
    Act->Button.m_State = UI_BUTTON_STATE_NORMAL;
    Act->Kind = Kind;
    Act->Fn = Fn;
    Act->Ctx = 0;
}

void UiActionSelfCheck(void) {
    UI_BUTTON_ACTION Sync;
    UI_BUTTON_ACTION Async;
    UI_BUTTON_ACTION Miss;
    int SyncOk;
    int AsyncOk;
    int MissFired;

    gStubSyncHit = 0;
    gStubAsyncHit = 0;

    StubInit(&Sync, UI_ACTION_SYNC, StubSync);
    (void)UiActionDispatch(&Sync, 1, 1); /* press hit */
    (void)UiActionDispatch(&Sync, 0, 1); /* release hit → Fn */
    SyncOk = gStubSyncHit;

    StubInit(&Async, UI_ACTION_ASYNC, StubAsync);
    (void)UiActionDispatch(&Async, 1, 1);
    (void)UiActionDispatch(&Async, 0, 1);
    AsyncOk = gStubAsyncHit;

    /* 松开未命中：不得调 Fn */
    gStubSyncHit = 0;
    StubInit(&Miss, UI_ACTION_SYNC, StubSync);
    (void)UiActionDispatch(&Miss, 1, 1);
    (void)UiActionDispatch(&Miss, 0, 0); /* release miss */
    MissFired = gStubSyncHit;

    DebugWrite("uiaction: sync=");
    DebugWrite(SyncOk ? "1" : "0");
    DebugWrite(" async=");
    DebugWrite(AsyncOk ? "1" : "0");
    DebugWrite(" miss=");
    DebugWrite(MissFired ? "1" : "0");
    DebugWrite("\n");
}
