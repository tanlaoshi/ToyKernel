/*
 * XhciDiagFormat.c — PR-S-xhcidag-1：PHOTO 统计串 / arms 日志
 *
 * 从 XhciDiag.c 原样搬家；不改语义。
 */
#include "XHCI/XhciInternal.h"


void XhciDiagFormat(char *Buf, int Max) {
    char Dig[12];
    int N = 0;
    UINT32 V[10];
    int vi;
    const char *Tags = "tikmucrdqx"; /* x=独占窗跳过 IRQ（excl-4） */
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
    V[9] = gStatIrqSkipped;
    Buf[N] = 0;
    for (vi = 0; vi < 10 && N + 14 < Max; vi++) {
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
    const char *P = "Boot: XHCI arms kbd=";
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
