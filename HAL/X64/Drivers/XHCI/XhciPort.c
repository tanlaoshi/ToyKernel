/*
 * XhciPort.c — PR-H-xhci-split-3：端口复位 / 上电 / PORTSC
 *
 * 从单体 XHCI.c 原样搬家；不改语义。
 */
#include "XHCI/XhciInternal.h"

UINT32 PortReg(UINT32 Port1) {
    return 0x400 + (Port1 - 1) * 0x10;
}

UINT8 PortSpeed(UINT32 Portsc) {
    return (UINT8)((Portsc >> PORTSC_SPEED_SHIFT) & 0xF);
}

UINT32 PortscNeutral(UINT32 State) {
    return (State & PORTSC_RO) | (State & PORTSC_RWS);
}

/*
 * 清 PORTSC 变更位（W1C）— 仅 QEMU/virt 路径使用。
 * 真机照片两轮：Neutral 清法与 SeaBIOS(PED|PP|CHANGE) 清法都会在
 * PED 已置位后把口打回 0x6E1/0xAE1（Polling）；故真机 ResetPort 不清变更。
 */
void PortscClearChange(UINT64 Ps) {
    UINT32 Val = ReadMmio32(Ps);
    WriteMmio32(Ps, PORTSC_PED | PORTSC_PP | (Val & PORTSC_CHANGE));
    Fence();
}

/*
 * 已连接口上电。
 * 刀1/刀2（笔记本 CCS=0，不动 NUC/工控已成功路径）：
 *   - 已有 CCS：行为与旧相同（真机仅 Stall 50ms）。
 *   - 全 0：PP all → 打 maxports → 多轮等待复扫 → 仍 0 则打前几口 PORTSC。
 */
void PowerConnectedPorts(void) {
    UINT32 p;
    UINT32 Surveyed = 0;
    int RealPc = !HalCpuIsHypervisor();
    int Round;
    UINT32 DumpLimit;

    for (p = 1; p <= gMaxPorts && p <= 32; p++) {
        UINT64 Ps = gOperationalBase + PortReg(p);
        UINT32 Val = ReadMmio32(Ps);
        if (Val & PORTSC_CCS) {
            Surveyed++;
            if (!(Val & PORTSC_PP)) {
                WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
            }
        }
    }
    if (Surveyed > 0) {
        /* NUC/工控：口上已有设备，保持原短等待 */
        if (RealPc) {
            StallMs(50);
        } else {
            volatile int D;
            for (D = 0; D < 50000; D++) {
            }
        }
        return;
    }

    BootLog("boot: xhci CCS=0, PP all\n");
    BootLogHex("boot: xhci maxports=", gMaxPorts, 2);
    for (p = 1; p <= gMaxPorts && p <= 32; p++) {
        UINT64 Ps = gOperationalBase + PortReg(p);
        UINT32 Val = ReadMmio32(Ps);
        if (!(Val & PORTSC_PP)) {
            WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
        }
    }

    if (!RealPc) {
        volatile int D;
        for (D = 0; D < 50000; D++) {
        }
        return;
    }

    /* 仅无 CCS：加长等待（USB3 口挂 USB2 鼠常见） */
    for (Round = 0; Round < 8; Round++) {
        StallMs(100);
        Surveyed = 0;
        for (p = 1; p <= gMaxPorts && p <= 32; p++) {
            UINT64 Ps = gOperationalBase + PortReg(p);
            UINT32 Val = ReadMmio32(Ps);
            if (!(Val & PORTSC_PP)) {
                WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
                Val = ReadMmio32(Ps);
            }
            if (Val & PORTSC_CCS) {
                Surveyed++;
            }
        }
        if (Surveyed > 0) {
            BootLogHex("boot: xhci CCS after wait=", Surveyed, 2);
            return;
        }
    }

    /* 普查已扫完全部口仍无 CCS；打齐最多 8 口 PORTSC（Intel 常 USB2/3 分口编号） */
    DumpLimit = gMaxPorts;
    if (DumpLimit > 8) {
        DumpLimit = 8;
    }
    for (p = 1; p <= DumpLimit; p++) {
        UINT32 Val = ReadMmio32(gOperationalBase + PortReg(p));
        char Pref[32];
        int n = 0;
        const char *S = "boot: xhci PORTSC";
        while (*S && n < 24) {
            Pref[n++] = *S++;
        }
        if (p >= 10) {
            Pref[n++] = (char)('0' + (p / 10));
            Pref[n++] = (char)('0' + (p % 10));
        } else {
            Pref[n++] = (char)('0' + p);
        }
        Pref[n++] = '=';
        Pref[n] = 0;
        BootLogHex(Pref, Val, 8);
    }
}
/*
 * 标准化端口复位（xHCI）：
 * USB2：PP → CCS → PR（勿写 PED=0）→ 等 PRC → 等 PED+CCS。
 * USB3：WPR。
 * 真机：已 PED 勿再 PR；成功后不清变更（sticky PRC）；仅 PED=0 时可清 sticky。
 */
/*
 * Force=0：真机已 PED+CCS 则跳过 PR（同 pass 内二次 PR 易打坏口）。
 * Force=1：鼠标等二次枚举须 PR（DisableSlot 后设备仍 PED，不重置会 cc=0x04）。
 */
int ResetPortEx(UINT32 Port1, int Force) {
    UINT64 Ps = gOperationalBase + PortReg(Port1);
    UINT32 Val = ReadMmio32(Ps);
    UINT32 SpeedHint = PortSpeed(Val);
    UINT32 Speed;
    int RealPc = !HalCpuIsHypervisor();
    int i;
    int t;
    int Ok;

    DiagChk("ResetPort.enter", 1, "PORTSC", Val, 8);

    if (RealPc && !Force && (Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
        DiagChk("ResetPort.already", 1, "PED+CCS skip PR", Val, 8);
        Speed = PortSpeed(Val);
        DiagChk("ResetPort.done", 1, "enabled", Speed, 2);
        return 1;
    }
    if (RealPc && Force && (Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
        BootLogHexV("boot: xhci port PR force=", Port1, 2);
    }

    /* 端口上电（勿在已连接时清 PED） */
    if (!(Val & PORTSC_PP)) {
        WriteMmio32(Ps, PortscNeutral(Val) | PORTSC_PP);
        Ok = RealPc ? WaitSetMs(Ps, PORTSC_PP, 50) : WaitSet(Ps, PORTSC_PP, 30000);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.PP", Ok && (Val & PORTSC_PP), "PP=1", Val, 8);
        if (!Ok) {
            EnumWhy("boot: why=PP timeout\n");
            return 0;
        }
    }

    if (!(Val & PORTSC_CCS)) {
        if (RealPc) {
            for (i = 0; i < 50; i++) {
                StallMs(10);
                Val = ReadMmio32(Ps);
                if (Val & PORTSC_CCS) {
                    break;
                }
            }
        } else {
            for (i = 0; i < 50000; i++) {
                Val = ReadMmio32(Ps);
                if (Val & PORTSC_CCS) {
                    break;
                }
            }
        }
        DiagChk("ResetPort.CCS", !!(Val & PORTSC_CCS), "CCS=1", Val, 8);
        if (!(Val & PORTSC_CCS)) {
            return 0;
        }
    }

    /* PED=0 时清 sticky 变更安全；便于重新 PR */
    if (RealPc && !(Val & PORTSC_PED) && (Val & PORTSC_CHANGE)) {
        WriteMmio32(Ps, PortscNeutral(Val) | (Val & PORTSC_CHANGE) | PORTSC_PP);
        Fence();
        StallMs(5);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.clrSticky", !(Val & PORTSC_PRC), "PRC=0", Val, 8);
    }

    SpeedHint = PortSpeed(Val);
    DiagChk("ResetPort.speed", SpeedHint != 0, "spd!=0", SpeedHint, 2);

    /*
     * 无条件热复位。USB2：写 PR，保留 PP；不要 &~PED（禁用口）。
     * 硬件会在复位过程中自行清 PED，完成后置 PED。
     */
    Val = PortscNeutral(ReadMmio32(Ps));
    if (SpeedHint >= 4) {
        WriteMmio32(Ps, Val | PORTSC_WPR | PORTSC_PP);
    } else {
        WriteMmio32(Ps, Val | PORTSC_PR | PORTSC_PP);
    }
    Fence();

    if (RealPc) {
        /*
         * 照片：PED OK → 任意 afterClr → Polling。成功后 leavePRC。
         * PRC 可先到而 PED 仍 0（0x002006E1），多等一会 PED。
         */
        Ok = WaitSetMs(Ps, PORTSC_PRC | PORTSC_WRC, 500);
        Val = ReadMmio32(Ps);
        DiagChk("ResetPort.PRC", Ok, "PRC|WRC", Val, 8);
        if (!Ok) {
            EnumWhy("boot: why=reset timeout\n");
            return 0;
        }
        if (!(Val & PORTSC_PED)) {
            Ok = WaitSetMs(Ps, PORTSC_PED, 1000);
            Val = ReadMmio32(Ps);
        }
        DiagChk("ResetPort.PED", (Val & PORTSC_PED) && (Val & PORTSC_CCS),
                "PED+CCS", Val, 8);
        if (!(Val & PORTSC_PED) || !(Val & PORTSC_CCS)) {
            if (!(Val & PORTSC_CCS)) {
                EnumWhy("boot: why=lost CCS\n");
            } else {
                EnumWhy("boot: why=not PED\n");
            }
            return 0;
        }
        DiagChk("ResetPort.leavePRC", 1, "sticky PRC", Val, 8);
        Speed = PortSpeed(Val);
        DiagChk("ResetPort.done", 1, "enabled", Speed, 2);
        StallMs(50);
        return 1;
    }

    Ok = WaitSet(Ps, PORTSC_PRC | PORTSC_WRC, 60000);
    Val = ReadMmio32(Ps);
    DiagChk("ResetPort.PRC", Ok, "PRC|WRC", Val, 8);
    if (!Ok) {
        return 0;
    }
    if (!(Val & PORTSC_PED)) {
        for (t = 0; t < 30000; t++) {
            Val = ReadMmio32(Ps);
            if (Val & PORTSC_PED) {
                break;
            }
        }
    }
    PortscClearChange(Ps);
    for (t = 0; t < 30000; t++) {
        Val = ReadMmio32(Ps);
        if ((Val & PORTSC_PED) && (Val & PORTSC_CCS)) {
            DiagChk("ResetPort.done", 1, "PED+CCS", Val, 8);
            return 1;
        }
    }
    DiagChk("ResetPort.PED", 0, "PED+CCS", ReadMmio32(Ps), 8);
    return 0;
}

int ResetPort(UINT32 Port1) {
    return ResetPortEx(Port1, 0);
}
