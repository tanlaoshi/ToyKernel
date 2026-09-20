/*
 * XhciMsc.c — PR-S-xhcimsc-1：MSC 核心（BringUp / Ready / Scan / 容量查询入口）
 *
 * Bulk→XhciMscBulk；BOT/扇区→XhciMscBot；配置认领→XhciMscClaim；
 * 根口编排→XhciMscClaimPorts。事件环 Bulk 完成仍在 XhciCore.c。
 * MSC 全局仍定义在 XhciCore.c（BSS 顺序影响 HID DMA 环地址；勿迁出）。
 */
#include "XHCI/XhciInternal.h"

/*
 * PR-H-msc-2/4：Bulk 环 Init；claim 后 Ready=1（仍无 SCSI）。
 */
int XhciMscBringUp(void) {
    if (!gMscBulkRingsInited) {
        InitRing(gBulkInRing, &gBulkIn, RING_SIZE);
        InitRing(gBulkOutRing, &gBulkOut, RING_SIZE);
        FlushDma(gBulkInRing, sizeof(gBulkInRing));
        FlushDma(gBulkOutRing, sizeof(gBulkOutRing));
        gMscBulkRingsInited = 1;
    }
    return gMscClaimed ? 0 : -1;
}

int XhciMscReady(void) {
    return gMscClaimed ? 1 : 0;
}

UINT32 XhciMscBlockCount(void) {
    return gMscCapacityOk ? gMscBlockCount : 0;
}

UINT32 XhciMscBlockSize(void) {
    return gMscCapacityOk ? gMscBlockSize : 0;
}

/*
 * PR-H-msc-3（热修）：只读根口 PORTSC 清点候选，**禁止 Address/Disable**。
 * NUC：未 Force PR 的 PED 口（如 0x11）Address 命令超时 → 命令环 sick →
 * MSI irq-stall → 鼠标假死。class/VID 留给 msc claim（可 Force PR）。
 */
int XhciMscScanPorts(void) {
    UINT32 P;
    int Found = 0;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        BootLog("Boot: MSC scan no hc\n");
        return -1;
    }

    BootLog("Boot: MSC scan begin (portsc only)\n");

    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps = ReadMmio32(gOperationalBase + PortReg(P));
        UINT8 Speed;

        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        if (gSlotId != 0 && P == gPort1) {
            BootLogHex("Boot: MSC scan skip kbd port=", P, 2);
            continue;
        }
        if (gMouseSlotId != 0 && P == gMousePort) {
            BootLogHex("Boot: MSC scan skip mouse port=", P, 2);
            continue;
        }
        if (gHubSlotId != 0 && P == gHubRootPort) {
            BootLogHex("Boot: MSC scan skip hub port=", P, 2);
            continue;
        }
        if (gMscClaimed && P == gMscPort) {
            BootLogHex("Boot: MSC scan skip claimed port=", P, 2);
            continue;
        }

        Speed = PortSpeed(Ps);
        BootLogHex("Boot: MSC scan port=", P, 2);
        BootLogHex("Boot: MSC scan speed=", Speed, 1);
        BootLogHex("Boot: MSC scan ped=", (Ps & PORTSC_PED) ? 1u : 0u, 1);
        BootLogHex("Boot: MSC scan portsc=", Ps, 8);
        if (!(Ps & PORTSC_PED)) {
            BootLog("Boot: MSC scan note: claim will Force PR\n");
            gPortNeedForcePr |= (1u << P);
        }
        Found++;
    }

    BootLogHex("Boot: MSC scan done n=", (UINT32)Found, 2);
    return Found;
}
