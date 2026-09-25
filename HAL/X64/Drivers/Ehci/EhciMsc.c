/*
 * EhciMsc.c — MSC 门面：Ready / Scan / Claim / Release / Present（PR-H-ehci-3）
 */
#include "EhciPrivate.h"
#include "ToySerialLog.h"

EHCI_CTRL *gEhciMscCtrl;
int gEhciMscInSense;
UINT32 gEhciMscTag;

int EhciMscReady(void) {
    return (gEhciMscCtrl && gEhciMscCtrl->MscOk && gEhciMscCtrl->MscCapacityOk)
               ? 1
               : 0;
}

UINT32 EhciMscBlockCount(void) {
    return (gEhciMscCtrl && gEhciMscCtrl->MscCapacityOk)
               ? gEhciMscCtrl->MscBlockCount
               : 0;
}

UINT32 EhciMscBlockSize(void) {
    return (gEhciMscCtrl && gEhciMscCtrl->MscCapacityOk)
               ? gEhciMscCtrl->MscBlockSize
               : 0;
}

int EhciMscScan(void) {
    int i;
    int Found = 0;

    if (!gEhciReady) {
        return -1;
    }
    for (i = 0; i < gEhciCount; i++) {
        EHCI_CTRL *C = &gEhci[i];
        UINT8 P;
        if (!C->Up || !C->Sched) {
            continue;
        }
        EhciSurveyCcs(C);
        for (P = 1; P <= C->NPorts; P++) {
            if (C->CcsMask & (1u << (P - 1))) {
                Found++;
            }
        }
        if (C->HubAddr) {
            Found++;
        }
    }
    ToyBootMarkUsb("Boot: EHCI MSC scan n=");
    {
        char Dig[3];
        Dig[0] = (char)('0' + (Found % 10));
        Dig[1] = 0;
        ToyBootMarkUsb(Dig);
        ToyBootMarkUsb("\n");
    }
    return Found;
}

int EhciMscClaim(void) {
    int i;

    if (!gEhciReady) {
        return -1;
    }
    if (gEhciMscCtrl && gEhciMscCtrl->MscOk && gEhciMscCtrl->MscCapacityOk) {
        return 1;
    }

    ToyBootMarkUsb("Boot: EHCI MSC claim begin\n");
    for (i = 0; i < gEhciCount; i++) {
        EHCI_CTRL *C = &gEhci[i];
        UINT8 P;

        if (!C->Up || !C->Sched) {
            continue;
        }
        if (C->HubAddr) {
            if (EhciMscClaimViaHub(C)) {
                return 1;
            }
            continue;
        }
        EhciSurveyCcs(C);
        for (P = 1; P <= C->NPorts; P++) {
            if ((C->CcsMask & (1u << (P - 1))) == 0) {
                continue;
            }
            if (EhciMscEnsureHub(C, P)) {
                return 1;
            }
        }
        for (P = 1; P <= C->NPorts; P++) {
            if (EhciMscEnsureHub(C, P)) {
                return 1;
            }
        }
    }
    ToyBootMarkUsb("Boot: EHCI MSC claim none\n");
    return 0;
}

int EhciMscRelease(void) {
    if (gEhciMscCtrl) {
        gEhciMscCtrl->MscOk = 0;
        gEhciMscCtrl->MscCapacityOk = 0;
        gEhciMscCtrl->MscBlockCount = 0;
        gEhciMscCtrl->MscBlockSize = 0;
        gEhciMscCtrl->MscAddr = 0;
    }
    gEhciMscCtrl = 0;
    ToyBootMarkUsb("Boot: EHCI MSC release\n");
    return 0;
}

int EhciMscPresent(void) {
    EHCI_CTRL *C = gEhciMscCtrl;
    UINT16 St;
    UINT16 Ch;

    if (!C || !C->MscOk) {
        return 0;
    }
    if (C->MscHubAddr && C->MscHubPort) {
        if (!EhciMscHubGetStatus(C, C->MscHubAddr, C->MscHubPort, &St, &Ch)) {
            return 0;
        }
        return (St & EHCI_HUB_STAT_CONNECT) ? 1 : 0;
    }
    if (C->Up) {
        EhciSurveyCcs(C);
        return C->CcsMask ? 1 : 0;
    }
    return 0;
}
