/*
 * E1000.c — Intel e1000 收发 / 链路查询（PR-S3-e1000-2）
 *
 * Setup 见 E1000Setup.c。
 */
#include "E1000.h"
#include "E1000Private.h"
#include "Hal.h"
#include "Net.h"
#include "ToySerialLog.h"

volatile UINT8 *gBar;
UINT64 gBarPhys;
UINT16 gPciDid;
UINT8 gPciBus;
UINT8 gPciDev;
UINT8 gPciFn;
E1000_RX_DESC *gE1000RxRing;
E1000_TX_DESC *gE1000TxRing;
UINT8 *gE1000RxBufs;
UINT8 *gE1000TxBuf;
UINT16 gE1000RxTail;
UINT16 gE1000TxTail;
UINT8 gE1000Mac[6];
int gE1000Ready;
int gE1000SetupOnce; /* FS Probe NET + [Mod] Network 会各扫一次；只 Setup 一轮 */
int gE1000UseIrq; /* PR-H4e-3：MSI 武装成功 */
static volatile UINT32 gStatIrq;
UINT32 gE1000TxOk;
UINT32 gE1000TxFail;
INT32 gE1000TxLastRc;

/*
 * PR-N-i219-tx3 单假说：I219 reset 后 unit hang — FEXTNVM11 MULR fix
 * + 哑元 TX 冲环。仅 156F；环基址已写后调用；不改 PHY。
 */
void E1000FlushI219Rings(void) {
    UINT32 Fext;
    UINT32 Tctl;
    E1000_TX_DESC *D;
    int Spin;
    int GotDd;

    if (gPciDid != E1000_DID_I219_LM || !gE1000TxRing || !gE1000TxBuf) {
        return;
    }

    Fext = MmioR32(E1000_REG_FEXTNVM11);
    MmioW32(E1000_REG_FEXTNVM11, Fext | E1000_FEXTNVM11_DISABLE_MULR_FIX);

    Tctl = MmioR32(E1000_REG_TCTL);
    MmioW32(E1000_REG_TCTL, Tctl | E1000_TCTL_EN);
    E1000ApplyI219TxDctl();

    D = &gE1000TxRing[0];
    ZeroMemory(gE1000TxBuf, 512);
    D->Addr = (UINT64)(UINTN)gE1000TxBuf;
    D->Length = 512;
    D->Cso = 0;
    D->Cmd = (UINT8)(E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS);
    D->Status = 0;
    D->Css = 0;
    D->Special = 0;
    Fence();
    MmioW32(E1000_REG_TDT, 1);

    Spin = 200000;
    while (Spin-- > 0 && !(D->Status & E1000_TX_DD)) {
        HalCpuRelax();
    }
    GotDd = (D->Status & E1000_TX_DD) ? 1 : 0;

    /* 保持 TCTL.EN：勿用冲环前的旧值（可能关 EN）盖回去 */
    MmioW32(E1000_REG_TCTL, Tctl | E1000_TCTL_EN);
    MmioW32(E1000_REG_TDH, 0);
    MmioW32(E1000_REG_TDT, 0);
    gE1000TxTail = 0;
    D->Addr = 0;
    D->Length = 0;
    D->Cmd = 0;
    D->Status = E1000_TX_DD;

    /*
     * PR-N-i219-tx5：勿恢复 Fext（MULR fix 常驻）。
     */
    (void)Fext;
    ToyLogNet(GotDd ? "Boot: I219 Flush OK\n" : "Boot: I219 Flush NoDD\n");
}

int E1000Ready(void) {
    return gE1000Ready;
}

void E1000GetMac(UINT8 Mac[6]) {
    CopyMemory(Mac, gE1000Mac, 6);
}

/*
 * PR-H4e-2：读 STATUS 链路/速度/双工（8254x / 82574 编码一致）。
 * 成功 0；未就绪 -1。Mbps=0 表示未知。
 */
int E1000GetLink(int *UpOut, UINT32 *MbpsOut, int *FullDuplexOut) {
    UINT32 St;
    UINT32 Sp;
    UINT32 Mbps = 0;

    if (!gE1000Ready || !gBar) {
        return -1;
    }
    St = MmioR32(E1000_REG_STATUS);
    Sp = (St & E1000_STATUS_SPEED_MASK) >> E1000_STATUS_SPEED_SHIFT;
    if (Sp == 0u) {
        Mbps = 10;
    } else if (Sp == 1u) {
        Mbps = 100;
    } else if (Sp == 2u) {
        Mbps = 1000;
    }
    if (UpOut) {
        *UpOut = (St & E1000_STATUS_LU) ? 1 : 0;
    }
    if (MbpsOut) {
        *MbpsOut = Mbps;
    }
    if (FullDuplexOut) {
        *FullDuplexOut = (St & E1000_STATUS_FD) ? 1 : 0;
    }
    return 0;
}

/* 82574 → "e1000e"；I219 → "i219"；其它 → "e1000" */
const char *E1000ChipName(void) {
    if (gPciDid == E1000_DID_82574L) {
        return "e1000e";
    }
    if (gPciDid == E1000_DID_I219_LM) {
        return "i219";
    }
    return "e1000";
}

int E1000SendFrame(const UINT8 *Frame, UINTN Len) {
    E1000_TX_DESC *D;
    UINTN Wire = Len;
    int Up = 0;

    if (!gE1000Ready || !Frame || Len < 14) {
        gE1000TxFail++;
        gE1000TxLastRc = -1;
        return -1;
    }
    if (E1000GetLink(&Up, 0, 0) != 0 || !Up) {
        gE1000TxFail++;
        gE1000TxLastRc = -4;
        return -1;
    }
    if (Wire > E1000_BUF_SIZE) {
        gE1000TxFail++;
        gE1000TxLastRc = -1;
        return -1;
    }
    if (Wire < 60) {
        Wire = 60;
    }

    D = &gE1000TxRing[gE1000TxTail];
    /* 不空等 DD：描述符忙则立刻失败，留给下次 Poll/ping 呼吸 */
    if (!(D->Status & E1000_TX_DD)) {
        gE1000TxFail++;
        gE1000TxLastRc = -2;
        return -1;
    }

    ZeroMemory(gE1000TxBuf, Wire);
    CopyMemory(gE1000TxBuf, Frame, Len);
    D->Addr = (UINT64)(UINTN)gE1000TxBuf;
    D->Length = (UINT16)Wire;
    D->Cso = 0;
    D->Cmd = (UINT8)(E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS);
    D->Status = 0;
    D->Css = 0;
    D->Special = 0;
    Fence();
    gE1000TxTail = (UINT16)((gE1000TxTail + 1u) % E1000_RING_COUNT);
    MmioW32(E1000_REG_TDT, gE1000TxTail);
    /* 异步：不在此等 DD（曾在关中断路径上假死键鼠） */
    gE1000TxOk++;
    gE1000TxLastRc = 0;
    return 0;
}

void E1000Poll(void) {
    UINT16 Next;
    int Left;

    if (!gE1000Ready) {
        return;
    }
    (void)MmioR32(E1000_REG_ICR);

    /* 护栏：关中断下若 Status 粘住 DD 会死循环，ping 表现为永远 "..." */
    Left = (int)E1000_RING_COUNT + 2;
    while (Left-- > 0) {
        Next = (UINT16)((gE1000RxTail + 1u) % E1000_RING_COUNT);
        if (!(gE1000RxRing[Next].Status & E1000_RX_DD)) {
            break;
        }
        if (gE1000RxRing[Next].Status & E1000_RX_EOP) {
            UINT16 Len = gE1000RxRing[Next].Length;
            UINT8 *Buf = gE1000RxBufs + (UINTN)Next * E1000_BUF_SIZE;
            if (Len >= 14) {
                NetInputFrame(Buf, Len);
            }
        }
        gE1000RxRing[Next].Status = 0;
        gE1000RxTail = Next;
        Fence();
        MmioW32(E1000_REG_RDT, gE1000RxTail);
    }
}

/* PR-H4e-3：MSI 入口；仍可被 NetPoll 备份调用同一 E1000Poll */
void E1000Irq(void) {
    gStatIrq++;
    E1000Poll();
}

int E1000IrqEnabled(void) {
    return (gE1000Ready && gE1000UseIrq) ? 1 : 0;
}
