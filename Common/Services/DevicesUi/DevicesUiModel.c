/*
 * DevicesUiModel.c — Device 表格式化 / 筛选（PR-DEV-6）
 */
#include "DevicesUiPrivate.h"

static char HexDigit(UINT32 V) {
    V &= 0xFu;
    return (char)(V < 10 ? ('0' + V) : ('a' + V - 10));
}

static void PutHex2(char *Dst, UINTN *N, UINTN Max, UINT32 V) {
    if (*N + 2 >= Max) {
        return;
    }
    Dst[(*N)++] = HexDigit(V >> 4);
    Dst[(*N)++] = HexDigit(V);
}

static void PutHex4(char *Dst, UINTN *N, UINTN Max, UINT32 V) {
    if (*N + 4 >= Max) {
        return;
    }
    Dst[(*N)++] = HexDigit(V >> 12);
    Dst[(*N)++] = HexDigit(V >> 8);
    Dst[(*N)++] = HexDigit(V >> 4);
    Dst[(*N)++] = HexDigit(V);
}

void DevicesUiFormatPci(const DEVICE_NODE *Dev, char *Out, UINTN Max) {
    UINTN N = 0;

    if (!Out || Max < 8 || !Dev) {
        if (Out && Max) {
            Out[0] = 0;
        }
        return;
    }
    Out[N++] = '[';
    PutHex2(Out, &N, Max, Dev->PciBus);
    if (N + 1 < Max) {
        Out[N++] = ':';
    }
    PutHex2(Out, &N, Max, Dev->PciDev);
    if (N + 1 < Max) {
        Out[N++] = '.';
    }
    PutHex2(Out, &N, Max, Dev->PciFn & 7u);
    if (N + 1 < Max) {
        Out[N++] = ']';
    }
    Out[N] = 0;
}

void DevicesUiFormatIds(const DEVICE_NODE *Dev, char *Out, UINTN Max) {
    UINTN N = 0;

    if (!Out || Max < 10 || !Dev) {
        if (Out && Max) {
            Out[0] = 0;
        }
        return;
    }
    PutHex4(Out, &N, Max, Dev->Vendor);
    if (N + 1 < Max) {
        Out[N++] = ':';
    }
    PutHex4(Out, &N, Max, Dev->Device);
    Out[N] = 0;
}

static int FiltMatch(const DEVICE_NODE *Dev, int Filt) {
    if (!Dev) {
        return 0;
    }
    if (Filt == DEVUI_FILT_BOUND) {
        return Dev->Bound ? 1 : 0;
    }
    if (Filt == DEVUI_FILT_FREE) {
        return Dev->Bound ? 0 : 1;
    }
    return 1;
}

void DevicesUiRebuildFilt(void) {
    int i;
    int N = 0;
    DEVICE_NODE *Dev;

    gDevUiFiltCount = 0;
    for (i = 0; i < gDevUiCount && N < DEVUI_MAP_MAX; i++) {
        Dev = DeviceGet(i);
        if (!FiltMatch(Dev, gDevUiFilt)) {
            continue;
        }
        gDevUiMap[N++] = i;
    }
    gDevUiFiltCount = N;
    if (gDevUiFiltCount <= 0) {
        gDevUiSel = 0;
        gDevUiScroll = 0;
        return;
    }
    for (i = 0; i < gDevUiFiltCount; i++) {
        if (gDevUiMap[i] == gDevUiSel) {
            break;
        }
    }
    if (i >= gDevUiFiltCount) {
        gDevUiSel = gDevUiMap[0];
        gDevUiScroll = 0;
    }
}

void DevicesUiReload(void) {
    gDevUiCount = DeviceCount();
    if (gDevUiSel >= gDevUiCount) {
        gDevUiSel = gDevUiCount > 0 ? gDevUiCount - 1 : 0;
    }
    DevicesUiRebuildFilt();
}
