/*
 * XhciMscClaimPorts.c — PR-S-xhcimsc-1：根口/hub claim 编排。
 * Force/Address/Hub 帮手在 XhciMscClaimPort.c。
 * MSC 全局仍定义在 XhciCore.c（BSS 顺序影响 HID DMA 环；勿迁出）。
 */
#include "XHCI/XhciInternal.h"

/*
 * PR-H-msc-4：单口 claim — Address（可 Force PR）+ SetConfig + Bulk IN/OUT。
 * 优先扫已有 hub 子口（键鼠经 hub 时 U 盘常在同 hub）；再扫其它根口。
 * 不 SCSI、不挂 FAT、不碰键鼠口。成功则保留 slot；Ready=1。
 */
int XhciMscClaimPorts(void) {
    UINT32 P;
    int Ok = 0;

    if (!gXhciStarted || gOperationalBase == 0 || gMaxPorts == 0) {
        BootLog("Boot: MSC claim no hc\n");
        return -1;
    }
    if (gMscClaimed && gMscScanSlot != 0) {
        BootLogHex("Boot: MSC claim already port=", gMscPort, 2);
        return 1;
    }

    BootLog("Boot: MSC claim begin\n");
    /*
     * excl-3：不再 FallbackToPoll("msc-claim")。
     * 事件环由 excl-1/2 单消费者 + 消费锁串行；claim 保持 dual/irq。
     */
    /*
     * 12:40 成功：Force !PED 0x05 → EnableSlot（可需 Recover 重试）→ hub → MSC。
     * soft-fail 会留下挂起 TRB → irq-stall；恢复为正常 CA+重试，并重武装 HID。
     */
    if (gXhciCmdSick) {
        BootLog("Boot: MSC claim recover cmd sick\n");
        RecoverCommandRing();
        gXhciCmdSick = 0;
        if (gSlotId != 0 && gIntrDci != 0) {
            QueueIntr();
        }
        if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
            QueueMouseIntr();
        }
    }
    (void)XhciMscBringUp();

    if (gMscScanSlot != 0) {
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    gMscClaimed = 0;
    gMscPort = 0;
    gMscRoute = 0;
    gMscHubSlot = 0;
    gMscTtPort = 0;
    gMscCapacityOk = 0;
    gMscBlockCount = 0;
    gMscBlockSize = 0;

    /* 键盘/鼠已走 hub：先扫子口找 MSC（与 U 盘同 hub 时） */
    if (gHubSlotId != 0 && EnumHubChildrenForMsc()) {
        Ok = 1;
        goto done;
    }

    for (P = 1; P <= gMaxPorts && P <= 32u; P++) {
        UINT32 Ps;
        UINT8 Speed;
        int Force;
        int HubRc;

        Ps = ReadMmio32(gOperationalBase + PortReg(P));
        if (!(Ps & PORTSC_CCS)) {
            continue;
        }
        if (gSlotId != 0 && P == gPort1) {
            continue;
        }
        if (gMouseSlotId != 0 && P == gMousePort) {
            continue;
        }
        if (gHubSlotId != 0 && P == gHubRootPort) {
            continue; /* 子口已在上面 EnumHubChildrenForMsc 试过 */
        }

        /*
         * MSC 认领必须稳：真机始终 Force PR 再 Address。
         * 82978dd「PED 直 Address」在台式常 cc=0x04，再 Force 易 Why=not PED 丢 U 盘。
         * QEMU（hypervisor）仍可 PED 直试（msc-8）；失败再 Force。
         */
        if (!MscClaimForceUntilPed(P, &Force)) {
            continue;
        }

        {
            int Arc = MscClaimAddressPort(P, Force, &Speed);

            if (Arc < 0) {
                break;
            }
            if (Arc == 0) {
                continue;
            }
        }

        gXferSlot = gMscScanSlot;
        if (GetDeviceDesc() < 0) {
            BootLogHex("Boot: MSC claim desc fail port=", P, 2);
            gPortNeedForcePr |= (1u << P);
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
            continue;
        }

        /*
         * 根口 hub：device class 9，或 class 0 但配置含 hub iface（真机常见）。
         * 认领后扫子口 MSC；本刀新认领且无 MSC 则释放，以便试下一根口 hub。
         */
        HubRc = MscClaimTryHubOnRoot(P, Speed, &Ok);
        if (HubRc == 1) {
            goto done;
        }
        if (HubRc == 0) {
            continue;
        }

        /* FinishClaim 会再 GetDeviceDesc；描述已在 gCtrlBuf，直接走配置 */
        if (XhciMscFinishClaim(P, Speed)) {
            Ok = 1;
            goto done;
        }
    }

    BootLog("Boot: MSC claim none\n");

done:
    /*
     * EnableSlot 超时 → cmd sick。excl-4：Command() 标 sick 前已 Recover+重武装 HID。
     * 此处再 Recover 清 sick，并 poll fallback（硬失败兜底；非 msc-claim 路径）。
     */
    if (gXhciCmdSick) {
        BootLog("Boot: MSC claim recover after sick\n");
        RecoverCommandRing();
        gXhciCmdSick = 0;
        XhciFallbackToPoll("cmd-sick");
        if (!HalCpuIsHypervisor()) {
            int i;

            for (i = 0; i < 32; i++) {
                ProcessEventsRealPc();
            }
            ServiceHidCompletions();
        }
    }
    if (gSlotId != 0 && gIntrDci != 0) {
        QueueIntr();
    }
    if (gMouseSlotId != 0 && gMouseIntrDci != 0) {
        QueueMouseIntr();
    }
    return Ok;
}
