/*
 * Batch — ToyGfxBatch 范例（PR-GFX-batch-0）
 * 排空开窗竞态后画一帧；点 Redraw 重画。
 */
#include <stdio.h>
#include <sched.h>
#include <ToyUi.h>
#include <ToyGfxBatch.h>

static void Draw(int Wid) {
    ToyGfxBatchBegin(Wid);
    ToyGfxBatchFillRect(16, 28, 200, 80, 0x00202060u);
    ToyGfxBatchDrawRect(16, 28, 200, 80, 0x00FFFFFFu);
    ToyGfxBatchDrawLine(16, 28, 216, 108, 0x00FFFF00u);
    ToyGfxBatchFillRect(240, 28, 80, 80, 0x00602020u);
    ToyGfxBatchDrawRect(240, 28, 80, 80, 0x00FFFFFFu);
    ToyGfxBatchEnd();
}

int main(void) {
    int Wid;
    int Ev;

    Wid = ToyUiCreateWindow("Batch", 480, 280);
    if (Wid < 0) {
        printf("batch: create fail\n");
        return 1;
    }
    ToyUiAddButton(Wid, 0, "Redraw");
    int First = 1;
    for (;;) {
        Ev = ToyUiPoll(Wid);
        if (Ev == TOY_UI_EVENT_CLOSE || Ev < 0) {
            break;
        }
        if (First && (Ev == TOY_UI_EVENT_NONE || Ev == TOY_UI_EVENT_CLICK)) {
            Draw(Wid);
            First = 0;
        }
        if (Ev == TOY_UI_BUTTON_EVENT(0)) {
            Draw(Wid);
        }
        sched_yield();
    }
    return 0;
}
