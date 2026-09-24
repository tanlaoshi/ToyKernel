/*
 * VirtioInputDiag.c — PR-V-input-diag：virtio-input 只读计数（不改发送/解析）
 *
 * 假说分桶（Shell `show input` / 串口 `Input:`）：
 *   A p=0              → HalInputPoll 未跑（常见：交互任务钉 AP 永 WFI）
 *   B p>0 且 ke=te=0   → 环不前进（DMA/barrier/队）
 *   C ke/te>0 且 push 涨、dq=0 → 上层未 dequeue
 *   D dq 涨仍无 UI     → Gui/坐标（交下一刀）
 */
#include "VirtioInputDiag.h"
#include "Hal.h"
#include "ToySerialLog.h"

static int gKeyboardOn;
static int gTabletOn;
static UINT32 gPollCount;
static UINT32 gKeyboardEvents;
static UINT32 gTabletEvents;
static UINT32 gKeyboardPush;
static UINT32 gMousePush;
static UINT32 gKeyboardDequeue;
static UINT32 gMouseDequeue;
static UINT32 gTickSamples;

void VirtioInputDiagSetPresent(int KeyboardOn, int TabletOn) {
    gKeyboardOn = KeyboardOn ? 1 : 0;
    gTabletOn = TabletOn ? 1 : 0;
}

void VirtioInputDiagNotePoll(UINT16 KeyboardEvents, UINT16 TabletEvents) {
    gPollCount++;
    gKeyboardEvents += (UINT32)KeyboardEvents;
    gTabletEvents += (UINT32)TabletEvents;
}

void VirtioInputDiagNoteKeyboardPush(void) {
    gKeyboardPush++;
}

void VirtioInputDiagNoteMousePush(void) {
    gMousePush++;
}

void VirtioInputDiagNoteKeyboardDequeue(void) {
    gKeyboardDequeue++;
}

void VirtioInputDiagNoteMouseDequeue(void) {
    gMouseDequeue++;
}

static void AppendStr(char *Buf, int *N, int Max, const char *S) {
    if (!Buf || !N || !S || Max < 2) {
        return;
    }
    while (*S && *N + 1 < Max) {
        Buf[(*N)++] = *S++;
    }
    Buf[*N] = 0;
}

static void AppendU32(char *Buf, int *N, int Max, UINT32 X) {
    char Dig[12];
    int T = 0;

    if (!Buf || !N || Max < 2) {
        return;
    }
    if (X == 0) {
        if (*N + 1 < Max) {
            Buf[(*N)++] = '0';
            Buf[*N] = 0;
        }
        return;
    }
    while (X && T < 10) {
        Dig[T++] = (char)('0' + (X % 10));
        X /= 10;
    }
    while (T > 0 && *N + 1 < Max) {
        Buf[(*N)++] = Dig[--T];
    }
    Buf[*N] = 0;
}

void VirtioInputDiagFormat(char *Buf, int Max) {
    int N = 0;
    UINT32 T0;
    UINT32 T1;

    if (!Buf || Max < 8) {
        return;
    }
    Buf[0] = 0;
    AppendStr(Buf, &N, Max, "virtio kbd=");
    AppendU32(Buf, &N, Max, (UINT32)gKeyboardOn);
    AppendStr(Buf, &N, Max, " tab=");
    AppendU32(Buf, &N, Max, (UINT32)gTabletOn);
    AppendStr(Buf, &N, Max, " p=");
    AppendU32(Buf, &N, Max, gPollCount);
    AppendStr(Buf, &N, Max, " ke=");
    AppendU32(Buf, &N, Max, gKeyboardEvents);
    AppendStr(Buf, &N, Max, " te=");
    AppendU32(Buf, &N, Max, gTabletEvents);
    AppendStr(Buf, &N, Max, " kp=");
    AppendU32(Buf, &N, Max, gKeyboardPush);
    AppendStr(Buf, &N, Max, " mp=");
    AppendU32(Buf, &N, Max, gMousePush);
    AppendStr(Buf, &N, Max, " kd=");
    AppendU32(Buf, &N, Max, gKeyboardDequeue);
    AppendStr(Buf, &N, Max, " md=");
    AppendU32(Buf, &N, Max, gMouseDequeue);
    /* BSP/AP tick：AP=0 且 p=0 → 交互核未醒（钉 AP + 无 timer） */
    T0 = (UINT32)HalCpuTicks(0);
    T1 = (UINT32)HalCpuTicks(1);
    AppendStr(Buf, &N, Max, " t0=");
    AppendU32(Buf, &N, Max, T0);
    AppendStr(Buf, &N, Max, " t1=");
    AppendU32(Buf, &N, Max, T1);
}

void VirtioInputDiagOnTimer(void) {
    char Buf[192];
    UINT64 Ticks;

    /* 仅 BSP 采样，且只打前 3 次（约 1s/2s/3s @10ms） */
    if (HalGetCpuId() != 0) {
        return;
    }
    if (gTickSamples >= 3u) {
        return;
    }
    Ticks = HalCpuTicks(0);
    if (Ticks == 0ull || (Ticks % 100ull) != 0ull) {
        return;
    }
    gTickSamples++;
    VirtioInputDiagFormat(Buf, (int)sizeof(Buf));
    ToyLogDrv("Input: ");
    ToyLogDrv(Buf);
    ToyLogDrv("\n");
}
