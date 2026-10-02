/*
 * XhciDiag.c — PR-H-xhci-split-2 / PR-S-xhcidag-1：诊断日志核心
 *
 * 从单体 XHCI.c 原样搬家；Format/LogArms 见 XhciDiagFormat.c。不改语义。
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
    /* gDiagQuiet：探测重试中的 Why= 不刷屏；字串仍留给最终失败路径 */
    if (gDiagQuiet) {
        return;
    }
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
    const char *P;
    int Milestone = 0;
    int Fail = 0;

    if (!Text) {
        return;
    }
    if (!HalCpuIsHypervisor()) {
        /* 真机：步进也上屏，便于 PHOTO */
        ToyBootMarkUsb(Text);
        return;
    }
    /* QEMU：默认只留 keyboard / mouse / irq=；步进需 XHCI_DIAG_VERBOSE=1 */
    if (DiagVerbose()) {
        ToyLogUsb(Text);
        return;
    }
    for (P = Text; *P; P++) {
        char C0 = P[0] | 0x20;
        char C1 = P[1] | 0x20;
        char C2 = P[2] | 0x20;
        char C3 = P[3] | 0x20;
        if (C0 == 'f' && C1 == 'a' && C2 == 'i' && C3 == 'l') {
            Fail = 1;
        }
        if (C0 == 't' && C1 == 'i' && C2 == 'm' && C3 == 'e' &&
            (P[4] | 0x20) == 'o' && (P[5] | 0x20) == 'u' && (P[6] | 0x20) == 't') {
            Fail = 1;
        }
        /* XHCI-HID / xhci-hid …（命名规范 ALL_CAPS；过滤须大小写不敏感） */
        if (C0 == 'x' && C1 == 'h' && C2 == 'c' && C3 == 'i' &&
            P[4] == '-' && (P[5] | 0x20) == 'h' && (P[6] | 0x20) == 'i' &&
            (P[7] | 0x20) == 'd') {
            Milestone = 1;
        }
        if (C0 == 'i' && C1 == 'r' && C2 == 'q' && P[3] == '=') {
            Milestone = 1;
        }
    }
    if (Milestone || Fail) {
        ToyLogUsb(Text);
    }
}
