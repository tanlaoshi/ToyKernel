/*
 * XhciIrq.c — PR-H-xhci-split-8：IRQ / Drain / dual-poll 切换
 *
 * 从单体 XHCI.c 原样搬家；不改语义。
 */
#include "XHCI/XhciInternal.h"

/* 清除中断管理器挂起位 */
void ImClearPending(void) {
    UINT32 Im = ReadMmio32(gRuntimeBase + 0x20);
    WriteMmio32(gRuntimeBase + 0x20, Im | 1u);
}

/* XHCI MSI-X/MSI 中断：事件处理；真机走 RealPc 路径（粘 EINT/CCS） */
void XhciIrq(void) {
    int RealPc = !HalCpuIsHypervisor();

    gStatIrq++;
    SpinLockAcquire(&gHidQueueLock);
    if (RealPc) {
        ProcessEventsRealPc();
    } else {
        ProcessEvents();
    }
    if (gIntrDone) {
        gIntrDone = 0;
        if (gIntrReportReady) {
            gIntrReportReady = 0;
            FlushDma(gReportBuf, sizeof(gReportBuf));
            KbdPush();
            gStatKbdPush++;
        }
        QueueIntr();
    }
    if (gMouseIntrDone) {
        gMouseIntrDone = 0;
        if (gMouseReportReady) {
            gMouseReportReady = 0;
            FlushDma(gMouseBuf, sizeof(gMouseBuf));
            MousePush();
            gStatMousePush++;
        }
        QueueMouseIntr();
    }
    if (gRuntimeBase != 0) {
        ImClearPending();
    }
    SpinLockRelease(&gHidQueueLock);
}

/* 开 USBCMD.INTE + IMAN.IE（真机 Start 故意只置了 RS） */
void EnableHostInterrupts(void) {
    UINT32 Cmd;

    if (gOperationalBase != 0) {
        Cmd = ReadMmio32(gOperationalBase);
        WriteMmio32(gOperationalBase, Cmd | USBCMD_RS | USBCMD_INTE);
    }
    if (gRuntimeBase != 0) {
        WriteMmio32(gRuntimeBase + 0x20, 3u); /* IE | IP(W1C) */
    }
}

/*
 * 排空事件环。
 * POLL / DUAL：盲 ProcessEvents×32（backup；真机 dual 时 q=0 也靠这条活）。
 * IRQ（PR-H-xhci-irq）：减为 ×1 + 门铃轻推；长时间无新 IRQ → FallbackToPoll。
 */
void XhciDrainEvents(void) {
    int i;
    int RealPc = !HalCpuIsHypervisor();
    int Passes = 32;
    static UINT32 sLastIrq;
    static UINT32 sIrqStall;

    gStatDrain++;

    /* PR-H-xhci-irq：有 IRQ 证据才维持轻量 Drain；停滞则回 poll */
    if (gIrqMode == XHCI_IRQ_MODE_IRQ) {
        if (gStatIrq != sLastIrq) {
            sLastIrq = gStatIrq;
            sIrqStall = 0;
        } else if (++sIrqStall > 200000u) {
            sIrqStall = 0;
            XhciFallbackToPoll("irq-stall");
            /* Fallback 已 Drain；下面按 POLL 再走一轮无妨 */
        } else {
            Passes = 1; /* 减 poll */
        }
    } else if (gIrqMode == XHCI_IRQ_MODE_DUAL) {
        /* q 涨起来后再升 IRQ（懒升；真机 q=0 永留 dual） */
        if (gStatIrq >= 3u && gUseIrq) {
            gIrqMode = XHCI_IRQ_MODE_IRQ;
            sLastIrq = gStatIrq;
            sIrqStall = 0;
            BootLog("boot: xhci irq=msi (irq)\n");
            Passes = 1;
        }
    }

    SpinLockAcquire(&gHidQueueLock);
    for (i = 0; i < Passes; i++) {
        if (RealPc) {
            ProcessEventsRealPc();
        } else {
            ProcessEvents();
        }
        if (gIntrDone) {
            gIntrDone = 0;
            if (gIntrReportReady) {
                gIntrReportReady = 0;
                FlushDma(gReportBuf, sizeof(gReportBuf));
                KbdPush();
                gStatKbdPush++;
            }
            QueueIntr();
        }
        if (gMouseIntrDone) {
            gMouseIntrDone = 0;
            if (gMouseReportReady) {
                gMouseReportReady = 0;
                FlushDma(gMouseBuf, sizeof(gMouseBuf));
                MousePush();
                gStatMousePush++;
            }
            QueueMouseIntr();
        }
    }
    /*
     * 禁止在持 gHidQueueLock 时 HidGetInputReport/ControlXfer：
     * WaitCommand 可达数百 ms 且 IF=1，定时器切到 Gui 再 Drain → 同锁死锁，
     * 家侧表现为桌面不能打字、短按电源无效（须长按强制关机）。
     * PHOTO 已证明中断 IN 可完成；门铃轻推即可，勿走 EP0 兜底。
     */
    if (RealPc) {
        if (gSlotId != 0 && gIntrDci != 0 && (gStatDrain & 0xFu) == 0) {
            RingDoorbell(gSlotId, gIntrDci);
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0 && (gStatDrain & 0x7u) == 0) {
            RingDoorbell(gMouseSlotId, gMouseIntrDci);
        }
    }
    if (gUseIrq && gRuntimeBase != 0) {
        ImClearPending();
    }
    SpinLockRelease(&gHidQueueLock);
    /* 释锁后再 GET_REPORT，避免与 WaitTransfer 嵌套抢同一把锁 */
    if (RealPc) {
        XhciPollKbdGetReport();
    }
}

/* dual/irq → 切回 poll 备份（关 host IE；不拆 PCI MSI 表亦可，避免半残状态） */
void XhciFallbackToPoll(const char *Why) {
    UINT32 Cmd;
    char Line[72];
    int n = 0;
    const char *P = "boot: xhci irq=poll (fallback)";
    const char *W = Why;

    gUseIrq = 0;
    gIrqMode = XHCI_IRQ_MODE_POLL;
    if (gOperationalBase != 0) {
        Cmd = ReadMmio32(gOperationalBase);
        WriteMmio32(gOperationalBase, (Cmd | USBCMD_RS) & ~USBCMD_INTE);
    }
    if (gRuntimeBase != 0) {
        WriteMmio32(gRuntimeBase + 0x20, 0); /* clear IE */
    }
    while (*P && n + 1 < (int)sizeof(Line)) {
        Line[n++] = *P++;
    }
    if (W && W[0] && n + 2 < (int)sizeof(Line)) {
        Line[n++] = ' ';
        while (*W && n + 1 < (int)sizeof(Line)) {
            Line[n++] = *W++;
        }
    }
    if (n + 1 < (int)sizeof(Line)) {
        Line[n++] = '\n';
    }
    Line[n] = 0;
    BootLog(Line); /* 真机 PHOTO 可见；勿只用 ToyLogUsb */
    XhciDrainEvents();
}

/*
 * PR-H-xhci-dual：试 MSI-X/MSI + host IE → DUAL。
 * Drain 在 DUAL 下仍盲排空（XhciDrainEvents 不看 gUseIrq 关排空）。
 * 失败由调用方 XhciFallbackToPoll。
 */
int XhciTryEnterDual(USB_CONTROLLER *Device) {
    if (gIrqMode == XHCI_IRQ_MODE_DUAL && gUseIrq) {
        return 1;
    }
    if (!Device) {
        return 0;
    }
    if (!PciEnableMsi(Device, VEC_XHCI)) {
        return 0;
    }
    EnableHostInterrupts();
    /* 先盲排空再开 gUseIrq，避免半开窗口丢完成 */
    gUseIrq = 0;
    XhciDrainEvents();
    gUseIrq = 1;
    gIrqMode = XHCI_IRQ_MODE_DUAL;
    BootLog("boot: xhci irq=msi (dual)\n"); /* PHOTO ring 可抄 */
    return 1;
}

XHCI_IRQ_MODE XhciIrqMode(void) {
    return gIrqMode;
}

/* PHOTO/Shell：mode=poll|dual|irq + t/i/k/m/u/s/c/r/d/q（q=IRQ 进入次数） */

/* Arm 后打一枪：期望的键鼠 slot/DCI，便于对照 s=. */

/*
 * QEMU：MSI/IOAPIC → DUAL。真机：试 dual；失败 → poll (fallback)，Drain 永不关。
 */
int XhciEnableIrq(USB_CONTROLLER *Device) {
    UINT8 Dest;

    if (gUseGetReport || (gSlotId == 0 && gMouseSlotId == 0)) {
        DebugWrite("XHCI: no interrupt EP, IRQ unused\n");
        gUseIrq = 0;
        gIrqMode = XHCI_IRQ_MODE_POLL;
        ToyLogUsb("boot: xhci irq=none\n");
        return 0;
    }

    /* PHOTO / show xhci：Arm 后计数清零 */
    gStatIntrEvt = 0;
    gStatMouseEvt = 0;
    gStatKbdPush = 0;
    gStatMousePush = 0;
    gStatXferAny = 0;
    gStatUnmatched = 0;
    gStatEvtRing = 0;
    gStatDrain = 0;
    gStatLastCc = 0;
    gStatLastSlot = 0;
    gStatLastEp = 0;
    gStatIrq = 0;
    gDiagXferLogged = 0;
    gDiagIntrCcLogged = 0;
    XhciDiagLogArms();

    /*
     * 真机 Arm：只 Queue，勿 Sync（Stop+SetDeq 曾弄死 kbd / PHOTO r=0）。
     */
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    /* Queue 产生的 Stopped 勿计入 PHOTO */
    gStatIntrEvt = 0;
    gStatMouseEvt = 0;
    gStatKbdPush = 0;
    gStatMousePush = 0;
    gStatXferAny = 0;
    gStatUnmatched = 0;
    gStatEvtRing = 0;
    gStatDrain = 0;
    gStatLastCc = 0;
    gStatLastSlot = 0;
    gStatLastEp = 0;

    if (XhciTryEnterDual(Device)) {
        XhciDrainEvents();
        return 1;
    }

    /* QEMU：再试 INTx→IOAPIC；真机 dual 失败则纯 poll backup */
    if (HalCpuIsHypervisor()) {
        Dest = HalCpuApicId(0);
        if (PciEnableIoApicIntx(Device, VEC_XHCI, Dest)) {
            EnableHostInterrupts();
            gUseIrq = 0;
            XhciDrainEvents();
            gUseIrq = 1;
            gIrqMode = XHCI_IRQ_MODE_DUAL;
            ToyLogUsb("boot: xhci irq=ioapic (dual)\n");
            return 1;
        }
    }

    DebugWrite("XHCI: MSI failed; poll drain backup\n");
    XhciFallbackToPoll("no-msi");
    return 0;
}

/* 返回是否武装了设备中断（DUAL/IRQ）；POLL 时仍靠 Drain */
int XhciUsesIrq(void) {
    return gUseIrq != 0;
}
