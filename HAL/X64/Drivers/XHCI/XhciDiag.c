/*
 * XhciDiag.c — PR-H-xhci-split-2：诊断日志 / PHOTO 统计格式化
 *
 * 从单体 XHCI.c 原样搬家；不改语义。
 */
#include "XHCI/XhciInternal.h"


/* PR-H-xhci-split-2：诊断相关全局（定义在本文件） */
const char *gEnumWhy;
UINT32 gDiagXferLogged;
UINT32 gDiagQuiet;
UINT32 gDiagIntrCcLogged;
UINT32 gCtrlFailLogged;


/*
 * 串口日志级别（默认安静）：
 *   make XHCI_DIAG_VERBOSE=1  → 全量 OK DiagChk + 逐步 BootMark
 *   默认 0                    → 只打 FAIL + 键鼠/hub 里程碑（好抄 PHOTO）
 */

int DiagVerbose(void) {
    return XHCI_DIAG_VERBOSE != 0;
}


void BootLogHex(const char *Prefix, UINT64 Value, int Digits) {
    char B[20];
    char Msg[56];
    int n = 0;
    int i = 0;

    HalSerialFormatHex(B, Value, Digits);
    while (Prefix[n] && n < 36) {
        Msg[n] = Prefix[n];
        n++;
    }
    while (B[i] && n < 54) {
        Msg[n++] = B[i++];
    }
    Msg[n++] = '\n';
    Msg[n] = 0;
    BootLog(Msg);
}

void BootLogV(const char *Text) {
    if (DiagVerbose()) {
        BootLog(Text);
    }
}

void BootLogHexV(const char *Prefix, UINT64 Value, int Digits) {
    if (DiagVerbose()) {
        BootLogHex(Prefix, Value, Digits);
    }
}

void BootMarkV(const char *Text) {
    if (DiagVerbose()) {
        ToyBootMarkUsb(Text);
    }
}

void EnumWhy(const char *Why) {
    gEnumWhy = Why;
    BootLog(Why);
}

/* 期望 vs 实际：默认只打 FAIL；VERBOSE=1 时 OK 也打 */
static void DiagAppend(char *Msg, int *N, int Cap, const char *S) {
    while (S && *S && *N < Cap - 1) {
        Msg[(*N)++] = *S++;
    }
}

void DiagChk(const char *Step, int Ok, const char *Want, UINT64 Got, int Digits) {
    char Msg[88];
    char Hex[20];
    int n = 0;

    /* 安静：关掉 OK；FAIL 的 want=/got= 仍上 BootLog（PHOTO 能抄），除非 gDiagQuiet */
    if (gDiagQuiet) {
        return;
    }
    if (Ok && !DiagVerbose()) {
        return;
    }
    DiagAppend(Msg, &n, (int)sizeof(Msg), Ok ? "xhci OK " : "xhci FAIL ");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Step);
    DiagAppend(Msg, &n, (int)sizeof(Msg), " want=");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Want);
    DiagAppend(Msg, &n, (int)sizeof(Msg), " got=");
    HalSerialFormatHex(Hex, Got, Digits);
    DiagAppend(Msg, &n, (int)sizeof(Msg), Hex);
    if (n < (int)sizeof(Msg) - 1) {
        Msg[n++] = '\n';
    }
    Msg[n] = 0;
    BootLog(Msg);
}

void DiagChkStr(const char *Step, int Ok, const char *Want, const char *Got) {
    char Msg[88];
    int n = 0;

    if (gDiagQuiet) {
        return;
    }
    if (Ok && !DiagVerbose()) {
        return;
    }
    DiagAppend(Msg, &n, (int)sizeof(Msg), Ok ? "xhci OK " : "xhci FAIL ");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Step);
    DiagAppend(Msg, &n, (int)sizeof(Msg), " want=");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Want);
    DiagAppend(Msg, &n, (int)sizeof(Msg), " got=");
    DiagAppend(Msg, &n, (int)sizeof(Msg), Got);
    if (n < (int)sizeof(Msg) - 1) {
        Msg[n++] = '\n';
    }
    Msg[n] = 0;
    BootLog(Msg);
}

const char *CmdTrbName(UINT32 Control) {
    switch ((Control >> 10) & 0x3F) {
    case TRB_ENABLE_SLOT:
        return "EnableSlot";
    case TRB_DISABLE_SLOT:
        return "DisableSlot";
    case TRB_ADDRESS_DEV:
        return "AddressDev";
    case TRB_CONFIG_EP:
        return "ConfigEP";
    case TRB_EVALUATE_CTX:
        return "EvalCtx";
    case TRB_RESET_EP:
        return "ResetEP";
    case TRB_STOP_EP:
        return "StopEP";
    case TRB_SET_TR_DEQ:
        return "SetTrDeq";
    default:
        return "Command";
    }
}


void BootLog(const char *Text) {
    if (!HalCpuIsHypervisor()) {
        ToyBootMarkUsb(Text);
        return;
    }
    ToyLogUsb(Text);
}


void XhciDiagFormat(char *Buf, int Max) {
    char Dig[12];
    int N = 0;
    UINT32 V[9];
    int vi;
    const char *Tags = "tikmucrdq";
    const char *Mode;

    if (!Buf || Max < 8) {
        return;
    }
    if (gIrqMode == XHCI_IRQ_MODE_DUAL) {
        Mode = "mode=dual irq=msi";
    } else if (gIrqMode == XHCI_IRQ_MODE_IRQ) {
        Mode = "mode=irq irq=msi";
    } else {
        Mode = "mode=poll";
    }
    while (*Mode && N + 1 < Max) {
        Buf[N++] = *Mode++;
    }
    V[0] = gStatXferAny;
    V[1] = gStatIntrEvt + gStatMouseEvt;
    V[2] = gStatKbdPush;
    V[3] = gStatMousePush;
    V[4] = gStatUnmatched;
    V[5] = gStatLastCc;
    V[6] = gStatEvtRing;
    V[7] = gStatDrain;
    V[8] = gStatIrq;
    Buf[N] = 0;
    for (vi = 0; vi < 9 && N + 14 < Max; vi++) {
        int t = 0;
        UINT32 X = V[vi];
        /* c 与 se 之间插入 se=；c 在 Tags[5] */
        if (vi == 5 && N + 16 < Max) {
            Buf[N++] = ' ';
            Buf[N++] = 's';
            Buf[N++] = '=';
            {
                UINT32 S = gStatLastSlot;
                if (S >= 100) {
                    S = 99;
                }
                Buf[N++] = (char)('0' + (S / 10));
                Buf[N++] = (char)('0' + (S % 10));
            }
            Buf[N++] = '.';
            {
                UINT32 E = gStatLastEp;
                if (E >= 100) {
                    E = 99;
                }
                Buf[N++] = (char)('0' + (E / 10));
                Buf[N++] = (char)('0' + (E % 10));
            }
        }
        Buf[N++] = ' ';
        Buf[N++] = Tags[vi];
        Buf[N++] = '=';
        if (X == 0) {
            Buf[N++] = '0';
            Buf[N] = 0;
            continue;
        }
        while (X && t < 10) {
            Dig[t++] = (char)('0' + (X % 10));
            X /= 10;
        }
        while (t > 0 && N + 1 < Max) {
            Buf[N++] = Dig[--t];
        }
        Buf[N] = 0;
    }
    /*
     * 一眼读相：鼠有键无 + s 落在鼠 DCI → 键中断 IN 没完成（不是「计数器坏了」）。
     * 期望键 DCI 常为 05；s=05.03 只说明最近事件是鼠标。
     */
    if (V[2] == 0 && V[3] > 0 && N + 18 < Max) {
        const char *H = " !kbdIN=0";
        while (*H && N + 1 < Max) {
            Buf[N++] = *H++;
        }
        Buf[N] = 0;
    }
}


void XhciDiagLogArms(void) {
    char Line[96];
    int n = 0;
    const char *P = "boot: xhci arms kbd=";
    while (*P && n < 28) {
        Line[n++] = *P++;
    }
    Line[n++] = (char)('0' + ((gSlotId / 10) % 10));
    Line[n++] = (char)('0' + (gSlotId % 10));
    Line[n++] = '/';
    Line[n++] = (char)('0' + ((gIntrDci / 10) % 10));
    Line[n++] = (char)('0' + (gIntrDci % 10));
    P = " mouse=";
    while (*P && n < 48) {
        Line[n++] = *P++;
    }
    Line[n++] = (char)('0' + ((gMouseSlotId / 10) % 10));
    Line[n++] = (char)('0' + (gMouseSlotId % 10));
    Line[n++] = '/';
    Line[n++] = (char)('0' + ((gMouseIntrDci / 10) % 10));
    Line[n++] = (char)('0' + (gMouseIntrDci % 10));
    Line[n++] = '\n';
    Line[n] = 0;
    BootLog(Line); /* 真机 PHOTO 可见 slot/DCI，对照 s= */
}

