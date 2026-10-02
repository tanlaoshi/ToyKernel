/*
 * EhciXfer.c — 异步 control（停 ASE → 挂 qTD → 开 ASE）（PR-H-ehci-2 · 2h）
 */
#include "EhciPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"

const char *gEhciLastErr = "none";
static char gTokErr[40];

void EhciDelay(int Loops) {
    while (Loops-- > 0) {
        HalCpuRelax();
    }
}

void EhciFlush(const void *Ptr, UINTN Size) {
    const UINT8 *P = (const UINT8 *)Ptr;
    UINTN Off;

    if (!Ptr || Size == 0) {
        return;
    }
    for (Off = 0; Off < Size; Off += 64) {
        __asm__ volatile("clflush (%0)" : : "r"(P + Off) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");
}

void EhciCopyBuf(UINT8 *D, const UINT8 *S, UINT32 N) {
    UINT32 i;
    for (i = 0; i < N; i++) {
        D[i] = S[i];
    }
}

void EhciPrepQtd(EHCI_QTD *T, UINT32 Next, UINT32 Pid, UINT32 Bytes,
                    UINT32 Dt, UINT64 BufPhys) {
    UINT32 i;

    T->Next = Next;
    T->AltNext = EHCI_LINK_TERMINATE;
    T->Token = EHCI_QTD_ACTIVE | (3u << 10) | (Pid << 8) | (Bytes << 16) |
               (Dt ? EHCI_QTD_DT : 0);
    T->Buf[0] = (UINT32)BufPhys;
    for (i = 1; i < 5; i++) {
        T->Buf[i] = 0;
        T->BufHi[i] = 0;
    }
    T->BufHi[0] = 0;
}

int EhciAsyncOff(EHCI_CTRL *C) {
    UINT32 Cmd;
    int Spin;

    Cmd = EhciR32(C->Op, EHCI_USBCMD);
    EhciW32(C->Op, EHCI_USBCMD, Cmd & ~EHCI_CMD_ASE);
    Spin = 800000;
    while (Spin-- > 0) {
        if ((EhciR32(C->Op, EHCI_USBSTS) & EHCI_STS_ASS) == 0) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

int EhciAsyncOn(EHCI_CTRL *C) {
    UINT32 Cmd;
    int Spin;

    /* 规范：ASE=0 时确保 ASYNCLISTADDR 指向 reclaim 头 */
    EhciW32(C->Op, EHCI_ASYNCLISTADDR, (UINT32)EhciPtrPhys(C->AsyncHead));
    Cmd = EhciR32(C->Op, EHCI_USBCMD);
    EhciW32(C->Op, EHCI_USBCMD, Cmd | EHCI_CMD_ASE);
    Spin = 800000;
    while (Spin-- > 0) {
        if (EhciR32(C->Op, EHCI_USBSTS) & EHCI_STS_ASS) {
            return 1;
        }
        HalCpuRelax();
    }
    return 0;
}

static UINT32 ReadTok(EHCI_QTD *T) {
    EhciFence();
    return *(volatile UINT32 *)&T->Token;
}

static void SetTokErr(EHCI_CTRL *C, UINT32 Tok) {
    char Hex[12];
    int n = 0;
    const char *P = "tok=";
    UINT32 St;

    while (*P) {
        gTokErr[n++] = *P++;
    }
    HalSerialFormatHex(Hex, Tok, 8);
    gTokErr[n++] = Hex[2];
    gTokErr[n++] = Hex[3];
    gTokErr[n++] = Hex[4];
    gTokErr[n++] = Hex[5];
    gTokErr[n++] = Hex[6];
    gTokErr[n++] = Hex[7];
    gTokErr[n++] = Hex[8];
    gTokErr[n++] = Hex[9];
    P = "/sts=";
    while (*P && n < 36) {
        gTokErr[n++] = *P++;
    }
    St = EhciR32(C->Op, EHCI_USBSTS);
    HalSerialFormatHex(Hex, St, 4);
    gTokErr[n++] = Hex[2];
    gTokErr[n++] = Hex[3];
    gTokErr[n++] = Hex[4];
    gTokErr[n++] = Hex[5];
    gTokErr[n] = 0;
    gEhciLastErr = gTokErr;
}

int EhciWaitQtd(EHCI_CTRL *C, EHCI_QTD *T) {
    int Spin = 8000000;

    while (Spin-- > 0) {
        UINT32 Tok = ReadTok(T);
        if ((Tok & EHCI_QTD_ACTIVE) == 0) {
            if (Tok & (EHCI_QTD_HALTED | (1u << 5) | (1u << 4) | (1u << 3))) {
                SetTokErr(C, Tok);
                return 0;
            }
            return 1;
        }
        {
            UINT32 St = EhciR32(C->Op, EHCI_USBSTS);
            if (St & (EHCI_STS_USBINT | EHCI_STS_USBERR)) {
                EhciW32(C->Op, EHCI_USBSTS,
                        St & (EHCI_STS_USBINT | EHCI_STS_USBERR));
            }
        }
        HalCpuRelax();
    }
    SetTokErr(C, ReadTok(T));
    return 0;
}

int EhciControlXfer(EHCI_CTRL *C, UINT8 Addr, UINT8 EpMax,
                    const USB_SETUP_PACKET *Setup, void *Data) {
    EHCI_QTD *SetupTd;
    EHCI_QTD *DataTd;
    EHCI_QTD *StatusTd;
    UINT32 Len;
    UINT32 DataPid;
    UINT32 StatusPid;
    UINT32 EpChar;

    if (!C || !C->Sched || !Setup) {
        gEhciLastErr = "ctrl bad arg";
        return -1;
    }
    Len = Setup->wLength;
    if (Len > 512) {
        gEhciLastErr = "ctrl len";
        return -1;
    }
    if (EpMax == 0) {
        EpMax = 64;
    }

    SetupTd = &C->Qtds[0];
    DataTd = &C->Qtds[1];
    StatusTd = &C->Qtds[2];

    EhciCopyBuf(C->SetupBuf, (const UINT8 *)Setup, 8);

    /* DTC + RL + Addr + MaxPkt；Speed 在 13:12；FS/LS control 置 C */
    {
        UINT32 Sp = C->XferSpeed;
        if (Sp > EHCI_SPEED_HS) {
            Sp = EHCI_SPEED_HS;
        }
        EpChar = ((UINT32)EpMax << 16) | (Sp << 12) | (1u << 14) | (0xFu << 28) |
                 (UINT32)Addr;
        if (Sp != EHCI_SPEED_HS) {
            EpChar |= (1u << 27); /* Control Endpoint Flag */
        }
    }

    if (Len && Data) {
        DataPid = (Setup->bmRequestType & 0x80u) ? EHCI_QTD_PID_IN
                                                 : EHCI_QTD_PID_OUT;
        StatusPid = (DataPid == EHCI_QTD_PID_IN) ? EHCI_QTD_PID_OUT
                                                 : EHCI_QTD_PID_IN;
        if ((Setup->bmRequestType & 0x80u) == 0) {
            EhciCopyBuf(C->CtrlBuf, (const UINT8 *)Data, Len);
        }
        EhciPrepQtd(SetupTd, (UINT32)EhciPtrPhys(DataTd), EHCI_QTD_PID_SETUP, 8, 0,
                EhciPtrPhys(C->SetupBuf));
        EhciPrepQtd(DataTd, (UINT32)EhciPtrPhys(StatusTd), DataPid, Len, 1,
                EhciPtrPhys(C->CtrlBuf));
        EhciPrepQtd(StatusTd, EHCI_LINK_TERMINATE, StatusPid, 0, 1, 0);
    } else {
        StatusPid = EHCI_QTD_PID_IN;
        EhciPrepQtd(SetupTd, (UINT32)EhciPtrPhys(StatusTd), EHCI_QTD_PID_SETUP, 8, 0,
                EhciPtrPhys(C->SetupBuf));
        EhciPrepQtd(StatusTd, EHCI_LINK_TERMINATE, StatusPid, 0, 1, 0);
    }

    if (!EhciAsyncOff(C)) {
        gEhciLastErr = "ase off";
        return -1;
    }
    C->CtrlQh->EpChar = EpChar;
    /* Mult=00；FS/LS split：HubAddr@16:22 Port@23:29（同 Linux ehci.h） */
    if (C->XferSpeed != EHCI_SPEED_HS && C->XferHubAddr) {
        C->CtrlQh->EpCap = ((UINT32)C->XferHubAddr << 16) |
                           ((UINT32)C->XferHubPort << 23);
    } else {
        C->CtrlQh->EpCap = 0;
    }
    C->CtrlQh->Current = 0;
    C->CtrlQh->AltNext = EHCI_LINK_TERMINATE;
    C->CtrlQh->Token = 0;
    {
        UINT32 i;
        for (i = 0; i < 5; i++) {
            C->CtrlQh->Buf[i] = 0;
            C->CtrlQh->BufHi[i] = 0;
        }
    }
    C->CtrlQh->Next = (UINT32)EhciPtrPhys(SetupTd);
    C->CtrlQh->Horiz =
        (UINT32)EhciPtrPhys(C->AsyncHead) | EHCI_LINK_TYPE_QH;
    C->AsyncHead->Horiz =
        (UINT32)EhciPtrPhys(C->CtrlQh) | EHCI_LINK_TYPE_QH;
    C->AsyncHead->Token = EHCI_QTD_HALTED;
    C->AsyncHead->Next = EHCI_LINK_TERMINATE;
    EhciFence();
    if (!EhciAsyncOn(C)) {
        gEhciLastErr = "ase on";
        return -1;
    }

    if (!EhciWaitQtd(C, SetupTd)) {
        return -1;
    }
    if (Len && Data) {
        if (!EhciWaitQtd(C, DataTd)) {
            return -1;
        }
    }
    if (!EhciWaitQtd(C, StatusTd)) {
        return -1;
    }

    if (!EhciAsyncOff(C)) {
        gEhciLastErr = "ase off2";
        return -1;
    }
    C->CtrlQh->Next = EHCI_LINK_TERMINATE;
    C->CtrlQh->Token = 0;
    EhciAsyncOn(C);

    if (Len && Data && (Setup->bmRequestType & 0x80u)) {
        EhciCopyBuf((UINT8 *)Data, C->CtrlBuf, Len);
    }
    return 0;
}

