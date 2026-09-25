/*
 * TtyUi.c — 串口会话缓冲 / 键入 TX
 */
#include "TtyUiPrivate.h"

char gTtyBuf[TTY_BUF_MAX];
UINTN gTtyLen;
int gTtyScroll;
char gTtyStatus[TTY_STATUS_MAX];
int gTtyOpen;

static void CopyStr(char *Dst, UINTN Max, const char *Src) {
    UINTN i;

    if (!Dst || Max == 0) {
        return;
    }
    for (i = 0; Src && Src[i] && i + 1 < Max; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

void TtySetStatus(const char *S) {
    CopyStr(gTtyStatus, sizeof(gTtyStatus), S ? S : "");
}

void TtySerialOut(const char *S) {
    if (S && S[0]) {
        HalSerialWrite(S);
    }
}

void TtyAppend(char C) {
    if (gTtyLen + 1 >= TTY_BUF_MAX) {
        UINTN Drop = TTY_BUF_MAX / 4;
        UINTN i;

        if (Drop < 64) {
            Drop = 64;
        }
        if (Drop >= gTtyLen) {
            gTtyLen = 0;
        } else {
            for (i = 0; i + Drop < gTtyLen; i++) {
                gTtyBuf[i] = gTtyBuf[i + Drop];
            }
            gTtyLen -= Drop;
        }
        gTtyBuf[gTtyLen] = 0;
    }
    gTtyBuf[gTtyLen++] = C;
    gTtyBuf[gTtyLen] = 0;
}

void TtyAppendStr(const char *S) {
    if (!S) {
        return;
    }
    while (*S) {
        TtyAppend(*S++);
    }
}

void TtyClampScroll(UINT32 VisLines) {
    UINTN Lines = 0;
    UINTN i;
    int MaxScroll;

    for (i = 0; i < gTtyLen; i++) {
        if (gTtyBuf[i] == '\n') {
            Lines++;
        }
    }
    if (gTtyLen > 0 && gTtyBuf[gTtyLen - 1] != '\n') {
        Lines++;
    }
    if (VisLines < 1) {
        VisLines = 1;
    }
    MaxScroll = (int)Lines - (int)VisLines;
    if (MaxScroll < 0) {
        MaxScroll = 0;
    }
    gTtyScroll = MaxScroll;
}

int TtyUiIsFocused(void) {
    return gTtyOpen && GuiFocusKind() == GUI_WIN_TTY;
}

#define TTY_PROMPT "tty> "

void TtyUiOpen(void) {
    gTtyOpen = 1;
    gTtyLen = 0;
    gTtyBuf[0] = 0;
    gTtyScroll = 0;
    /* ASCII only：点阵字体无 UTF-8 字形 */
    TtyAppendStr("ToyOS TTY - serial session (not boot log)\n");
    TtyAppendStr("Type to TX; RX while focused. Esc=clear.\n\n");
    if (HalSerialPresent()) {
        TtySetStatus("serial: present");
    } else {
        TtySetStatus("serial: no COM1 (USB-UART tee may still work)");
    }
    TtyAppendStr(TTY_PROMPT);
    /* 与 DevicesUiOpen 相同：开窗期焦点已在 OpenChromeDefer 设好，只画不 Raise */
    TtyPaint();
}

void TtyUiClose(void) {
    gTtyOpen = 0;
}

void TtyUiOnChar(char C) {
    char One[2];

    if (C < 32 || C > 126) {
        return;
    }
    One[0] = C;
    One[1] = 0;
    TtyAppend(C);
    TtySerialOut(One);
    TtyUiRepaint();
}

void TtyUiOnEnter(void) {
    /*
     * 须发 CR+LF：只发 \\r 时主机串口光标回行首、不下移，
     * 下一条输出会盖住刚键入的一行（virt -nographic / CoolTerm 同）。
     * ShellTask 侧已吞「CR 后紧跟的 LF」，不会双 Enter。
     */
    TtyAppend('\n');
    TtySerialOut("\r\n");
    TtyAppendStr(TTY_PROMPT);
    TtyUiRepaint();
}

void TtyUiOnBackspace(void) {
    UINTN LineStart;
    UINTN i;
    UINTN PromptLen;

    if (gTtyLen == 0) {
        return;
    }
    PromptLen = sizeof(TTY_PROMPT) - 1;
    LineStart = 0;
    for (i = 0; i < gTtyLen; i++) {
        if (gTtyBuf[i] == '\n') {
            LineStart = i + 1;
        }
    }
    if (gTtyLen <= LineStart + PromptLen) {
        return;
    }
    if (gTtyBuf[gTtyLen - 1] == '\n') {
        return;
    }
    gTtyLen--;
    gTtyBuf[gTtyLen] = 0;
    TtySerialOut("\b \b");
    TtyUiRepaint();
}

void TtyUiOnEscape(void) {
    gTtyLen = 0;
    gTtyBuf[0] = 0;
    TtySetStatus("buffer cleared");
    TtyAppendStr(TTY_PROMPT);
    TtyUiRepaint();
}

void TtyUiOnRxChar(char C) {
    if (C == '\r') {
        TtyAppend('\n');
    } else if (C == '\n') {
        if (gTtyLen == 0 || gTtyBuf[gTtyLen - 1] != '\n') {
            TtyAppend('\n');
        }
    } else if (C == '\b' || C == 127) {
        if (gTtyLen > 0 && gTtyBuf[gTtyLen - 1] != '\n') {
            gTtyLen--;
            gTtyBuf[gTtyLen] = 0;
        }
    } else if (C >= 32 && C <= 126) {
        TtyAppend(C);
    } else {
        return;
    }
    TtyUiRepaint();
}

void TtyUiOnClick(UINT32 X, UINT32 Y) {
    (void)X;
    (void)Y;
}
