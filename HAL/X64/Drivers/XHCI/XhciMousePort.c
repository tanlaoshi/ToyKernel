/*
 * XhciMousePort.c — PR-F-xhci-4：其它根口绑鼠标（编排）
 *
 * Claim / FinishEp 见 XhciMousePortClaim.c。鼠标全局仍在 Xhci.c。
 */
#include "XHCI/XhciInternal.h"

int InitMouseOnPort(UINT32 Port1) {
    UINT8 Speed = 0;
    int Claim;

    Claim = InitMouseClaimPort(Port1, &Speed);
    if (Claim >= 0) {
        return Claim;
    }

    if (!SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
        DebugWrite("XHCI: mouse config failed\n");
        /* 再 Force PR + 重 Address 一次（port4 真鼠曾卡在 cfg） */
        if (!HalCpuIsHypervisor()) {
            UINT32 Old = gMouseSlotId;
            BootLogV("Boot: XHCI mouse root retry\n");
            DisableSlot(Old);
            gMouseSlotId = 0;
            if (ResetPortEx(Port1, 1)) {
                if (!HalCpuIsHypervisor()) {
                    StallMs(20);
                }
                Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(Port1)));
                if (AddressDeviceOnPort(Port1, Speed, &gMouseSlotId, gMouseDevCtx,
                                        0, 0, 0, 0, 0) &&
                    SetupHidDevice(gMouseSlotId, gMouseDevCtx, Speed, ParseConfigMouse, 1)) {
                    /* fall through to EP setup below */
                } else {
                    if (gMouseSlotId) {
                        DisableSlot(gMouseSlotId);
                    }
                    gMouseSlotId = 0;
                    return 0;
                }
            } else {
                return 0;
            }
        } else {
            gPortNeedForcePr |= (1u << Port1);
            DisableSlot(gMouseSlotId);
            gMouseSlotId = 0;
            return 0;
        }
    }

    return InitMouseFinishEp(Port1, Speed);
}
