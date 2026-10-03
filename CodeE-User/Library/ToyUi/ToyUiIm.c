/*
 * ToyUiIm.c — 立即模式薄封装：Poll / Label / Button / Check
 * PR-UI-im-0/1；≤300 行；不改 ToyUi.h
 *
 * Title/Label 仅在变化时 SetLabel（避免每帧 damage 闪屏）；
 * 应用每帧末须 sched_yield（勿只在 Idle 时让出）。
 */
#include <ToyUiIm.h>

#define TOY_UI_IM_CHK_X  16u
#define TOY_UI_IM_CHK_Y0 28u
#define TOY_UI_IM_CHK_DY 24u

typedef struct {
    int Active;
    int Wid;
    int Ev;
    int BtnN;
    int ChkN;
    int Closed;
    int Idle;
    char Title[TOY_UI_IM_LAB_MAX];
    int TitleOn;
    char Lab[TOY_UI_IM_BTN_MAX][TOY_UI_IM_LAB_MAX];
    int LabOn[TOY_UI_IM_BTN_MAX];
    int ChkOn[TOY_UI_IM_CHK_MAX];
} TOY_UI_IM;

static TOY_UI_IM s_Im;

static int StrEq(const char *A, const char *B) {
    unsigned I;

    if (!A || !B) {
        return A == B;
    }
    for (I = 0; A[I] || B[I]; I++) {
        if (A[I] != B[I]) {
            return 0;
        }
    }
    return 1;
}

static void CopyLab(char *Dst, const char *Src) {
    unsigned I;

    if (!Dst) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    for (I = 0; I + 1 < TOY_UI_IM_LAB_MAX && Src[I]; I++) {
        Dst[I] = Src[I];
    }
    Dst[I] = 0;
}

static int ApplyLabel(const char *Text) {
    if (!s_Im.Active || !Text || !Text[0]) {
        return -1;
    }
    if (s_Im.TitleOn && StrEq(s_Im.Title, Text)) {
        return 0;
    }
    if (ToyUiSetLabel(s_Im.Wid, Text) != 0) {
        return -1;
    }
    CopyLab(s_Im.Title, Text);
    s_Im.TitleOn = 1;
    return 0;
}

int ToyUiImBegin(int Wid, const char *Title) {
    if (Wid < 0) {
        s_Im.Active = 0;
        return -1;
    }
    s_Im.Active = 1;
    s_Im.Wid = Wid;
    s_Im.BtnN = 0;
    s_Im.ChkN = 0;
    s_Im.Ev = ToyUiPoll(Wid);
    s_Im.Closed = (s_Im.Ev == TOY_UI_EVENT_CLOSE) ? 1 : 0;
    s_Im.Idle = (s_Im.Ev == TOY_UI_EVENT_NONE) ? 1 : 0;
    if (Title && Title[0]) {
        (void)ApplyLabel(Title);
    }
    return 0;
}

int ToyUiImClosed(void) {
    return (s_Im.Active && s_Im.Closed) ? 1 : 0;
}

int ToyUiImIdle(void) {
    return (s_Im.Active && s_Im.Idle) ? 1 : 0;
}

int ToyUiImLabel(const char *Text) {
    return ApplyLabel(Text);
}

int ToyUiImButton(const char *Label) {
    int Id;
    int Need;

    if (!s_Im.Active || !Label || s_Im.BtnN >= TOY_UI_IM_BTN_MAX) {
        return 0;
    }
    Id = TOY_UI_BUTTON_ID_MIN + s_Im.BtnN;
    Need = 0;
    if (!s_Im.LabOn[Id] || !StrEq(s_Im.Lab[Id], Label)) {
        Need = 1;
    }
    if (Need) {
        if (ToyUiAddButton(s_Im.Wid, Id, Label) != 0) {
            return 0;
        }
        CopyLab(s_Im.Lab[Id], Label);
        s_Im.LabOn[Id] = 1;
    }
    s_Im.BtnN++;
    if (s_Im.Ev == TOY_UI_BUTTON_EVENT(Id)) {
        return 1;
    }
    return 0;
}

int ToyUiImCheck(int *Checked) {
    int Id;
    int Now;
    unsigned Y;

    if (!s_Im.Active || s_Im.ChkN >= TOY_UI_IM_CHK_MAX) {
        return 0;
    }
    Id = s_Im.ChkN;
    if (!s_Im.ChkOn[Id]) {
        Y = TOY_UI_IM_CHK_Y0 + (unsigned)Id * TOY_UI_IM_CHK_DY;
        if (ToyUiAddCheckBox(s_Im.Wid, Id, TOY_UI_IM_CHK_X, Y) != 0) {
            return 0;
        }
        s_Im.ChkOn[Id] = 1;
        if (Checked) {
            (void)ToyUiSetCheckBox(s_Im.Wid, Id, *Checked ? 1 : 0);
        }
    }
    s_Im.ChkN++;
    if (s_Im.Ev == TOY_UI_CHECK_EVENT(Id)) {
        Now = ToyUiGetCheckBox(s_Im.Wid, Id);
        if (Checked) {
            *Checked = (Now > 0) ? 1 : 0;
        }
        return 1;
    }
    if (Checked) {
        Now = ToyUiGetCheckBox(s_Im.Wid, Id);
        if (Now >= 0 && Now != (*Checked ? 1 : 0)) {
            (void)ToyUiSetCheckBox(s_Im.Wid, Id, *Checked ? 1 : 0);
        }
    }
    return 0;
}

void ToyUiImEnd(void) {
    s_Im.Active = 0;
    s_Im.Wid = -1;
    s_Im.Ev = TOY_UI_EVENT_NONE;
}
