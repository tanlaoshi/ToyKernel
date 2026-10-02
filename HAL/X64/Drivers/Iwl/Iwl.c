/*
 * Iwl.c — Setup / 状态 / 后台起站（PR-S-iwl-split-2）
 */
#include "IwlPrivate.h"
#include "Hal.h"
#include "HalSerial.h"
#include "ToySerialLog.h"
#include "VirtualMemory.h"
#include "Net.h"

int gIwlReady;
int gIwlFwOk;
int gIwlBarOk;
int gIwlAlive;
int gIwlAssociated;
int gIwlWpa2Ok;
int gDatTxLogged;
int gDatRxLogged;
int gDtxLogged;
int gRxMicLogged;
int gRxMicU;
int gRxDiscLogged;
int gRxOffLogged;
int gRxLlcLogged;
int gRxStLogged;
UINT16 gIwlDid;
UINT8 gIwlBus;
UINT8 gIwlDev;
UINT8 gIwlFn;
UINT8 gIwlMac[6];
UINT8 gIwlBssid[6];
UINT16 gIwlAid;
UINT8 gIwlPtk[16];
UINT8 gIwlGtk[32];
UINT8 gIwlGtkAlt[16];
UINT8 gIwlGtkId;
UINT8 gIwlGtkAltOk;
UINT8 gIwlGtkLen;
UINT8 gIwlGroupCipher = 0x04; /* 默认 CCMP */
UINT8 gIwlTxStaId = IWL_AUX_STA_ID;

static void IwlMakeLocalMac(void) {
    /* NVM 读后续补；先用本地管理地址避免全 0 */
    gIwlMac[0] = 0x02;
    gIwlMac[1] = 0x54;
    gIwlMac[2] = 0x4F;
    gIwlMac[3] = 0x59;
    gIwlMac[4] = (UINT8)gIwlBus;
    gIwlMac[5] = (UINT8)gIwlDev;
}

static int IwlBringUpSta(void) {
    int Hw;

    if (!VirtualMemoryEnabled()) {
        IwlLogStage("bar=novm");
        return 0;
    }
    Hw = IwlHwStart();
    if (Hw == -1) {
        IwlLogStage("prep=fail");
        return 0;
    }
    if (Hw != 1) {
        IwlLogStage("apm=fail");
        return 0;
    }
    /* Linux：nic_init（RX）在灌固件前，ALIVE 走 RX 通知 */
    if (!IwlNicInit()) {
        IwlLogStage("nic=fail");
        return 0;
    }
    {
        int Fw = IwlFwParseAndLoad();
        if (Fw != 1) {
            if (Fw == -2) {
                IwlLogStage("init_alive=fail");
            } else if (Fw == -1) {
                IwlLogStage("fwload=fail");
            } else {
                IwlLogStage("alive=fail");
            }
            return 0;
        }
    }
    if (!IwlCfgLoad()) {
        IwlLogStage("cfg=miss");
        return 0;
    }
    (void)IwlPmkPrepare(); /* 扫描前算 PMK，握手窗口只做 PTK */
    /*
     * 刀 #136：#135 在 scan=ok 之后才 mac=use。
     * 认证帧地址已是 94:B8:6D，SCAN_CFG 仍是本地地址 → auth=to n=18 f=80（只有 beacon）。
     * 扫描配置和认证帧用同一块芯片地址。
     */
    IwlReadHwMac();
    IwlApplyHwMac();
    if (!IwlMvmPostAlive()) {
        /* mvm=* 已打；仍尝试 scan 以观察 RX */
    }
    if (!IwlScanRun()) {
        return 0;
    }
    /* 刀 #132：Auth 无 MAC；#139/#140：assoc=ok 后再 Prep。接着 AP STA + EAPOL */
    if (!IwlAssocRun()) {
        IwlLogStage("assoc=fail");
        return 0;
    }
    IwlLogStage("post=sta");
    (void)IwlMacCtxtAssoc();
    IwlLogStage("post=eap");
    if (!IwlEapolRun()) {
        IwlLogStage("wpa2=fail");
        return 0;
    }
    return 1;
}

static int gIwlBgPending;
static int gIwlBgDone;
static int gIwlBgPhase; /* 0=待起/读 FW；1=待 BringUpSta */
/* PR-BOOT-fast-3：0=FS/Net 早 Probe 跳过；Worker HalIwlClaim 前打开 */
static int gIwlBootClaimAllowed;

void IwlAllowBootClaim(void) {
    gIwlBootClaimAllowed = 1;
}

int IwlStartSta(void) {
    return IwlBringUpSta();
}

int IwlBgBusy(void) {
    return !gIwlBgDone && (gIwlBgPending || gIwlBgPhase != 0);
}

int IwlBgStep(void) {
    if (gIwlBgDone) {
        return 1;
    }
    if (!gIwlBgPending && gIwlBgPhase == 0) {
        return 1;
    }

    /* 相 0：读 FW 后立刻还给 Worker，避免与上片粘成一次超长阻塞 */
    if (gIwlBgPhase == 0) {
        gIwlBgPending = 0;
        IwlLogStage("bg=start");
        if (!gIwlFwOk) {
            (void)IwlFwTryLoad();
        }
        if (!gIwlFwOk || !gIwlBarOk) {
            IwlLogStage(gIwlFwOk ? "bar=miss" : "fw=miss");
            gIwlBgDone = 1;
            IwlLogBound();
            return 1;
        }
        gIwlBgPhase = 1;
        IwlLogStage("bg=fw");
        return 0;
    }

    IwlLogStage("bg=up");
    (void)IwlBringUpSta();
    gIwlBgPhase = 0;
    gIwlBgDone = 1;
    IwlLogStage("bg=done");
    IwlLogBound();
    return 1;
}

int IwlSetup(void) {
    UINT8 Bus;
    UINT8 Dev;
    UINT8 Fn;
    UINT16 Did = 0;
    UINT64 Bar = 0;

    if (gIwlReady) {
        return 1;
    }
    /*
     * PR-BOOT-fast-3：勿在 FileSystem/Network 模块里 Claim。
     * 早 Probe 会拖 I219 等同批 NET，且 BAR 认领应在桌面之后由 Worker 发起。
     */
    if (!gIwlBootClaimAllowed) {
        return 0;
    }
    if (!IwlPciFind(&Bus, &Dev, &Fn, &Bar, &Did)) {
        return 0;
    }
    gIwlBus = Bus;
    gIwlDev = Dev;
    gIwlFn = Fn;
    gIwlDid = Did;
    /* FW 大文件延到 IwlBgStep，Claim 只认 PCI+映 BAR */
    if (Bar && IwlMapBar(Bar)) {
        IwlMakeLocalMac();
        gIwlBarOk = 1;
    }
    gIwlReady = 1;
    /*
     * 刀 #114/#115：不在 Probe/开机路径上片关联（可卡数十秒）；
     * 亦不在此读 2.4MB UCODE（Live USB 上可卡数秒）。
     * Worker 后台 IwlBgStep；失败也不挡桌面。
     */
    if (gIwlBarOk) {
        gIwlBgPending = 1;
        /* #129：摘要留 bg=done，避免开机打两次 Bound */
    } else {
        IwlLogStage("defer=skip");
        IwlLogBound();
    }
    return 1;
}

int IwlReady(void) {
    return gIwlReady;
}

int IwlAssociated(void) {
    return gIwlAssociated && gIwlWpa2Ok;
}

void IwlGetMac(UINT8 Mac[6]) {
    int i;
    if (!Mac) {
        return;
    }
    for (i = 0; i < 6; i++) {
        Mac[i] = gIwlReady ? gIwlMac[i] : 0;
    }
}

UINT16 IwlPciDid(void) {
    return gIwlDid;
}

int IwlFwLoaded(void) {
    return gIwlFwOk;
}
