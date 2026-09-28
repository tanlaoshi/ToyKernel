/*
 * IwlPrivate.h — wifi-2 内部状态（对照 FreeBSD iwm / Linux iwlwifi，只读）
 */
#ifndef IWL_PRIVATE_H
#define IWL_PRIVATE_H

#include "Iwl.h"
#include "IwlRegs.h"

#define IWL_FW_PATH   "FW/IWL8265.UCODE"
#define IWL_CFG_PATH  "FW/WIFI.CFG"

typedef struct {
    const UINT8 *Data;
    UINT32 Len;
    UINT32 Offset;
} IWL_FW_SEC;

typedef struct {
    IWL_FW_SEC Sec[IWL_FW_SEC_MAX];
    int NumSec;
    int DualCpus;
    UINT32 PagingMemSize; /* TLV_PAGING；0=无 CPU2 分页镜像 */
} IWL_FW_IMG;

typedef struct {
    UINT8 Code;
    UINT8 Flags;
    UINT8 Idx;
    UINT8 Qid;
} IWL_CMD_HDR;

typedef struct {
    UINT8 Opcode;
    UINT8 GroupId;
    UINT8 Idx;
    UINT8 Qid;
    UINT16 Length;
    UINT8 Reserved;
    UINT8 Version;
} __attribute__((packed)) IWL_CMD_HDR_WIDE;

typedef struct {
    UINT32 LenNFlags;
    IWL_CMD_HDR Hdr;
    UINT8 Data[];
} IWL_RX_PKT;

typedef struct {
    UINT32 Lo;
    UINT16 HiNLen; /* [3:0]=addr[35:32] [15:4]=len */
} __attribute__((packed)) IWL_TFD_TB;

typedef struct {
    UINT8 Reserved[3];
    UINT8 NumTbs;
    IWL_TFD_TB Tb[IWL_TFD_NUM_TBS];
    UINT32 Pad;
} __attribute__((packed)) IWL_TFD;

typedef struct {
    UINT8 Bssid[6];
    UINT8 Chan;
    INT8 Rssi;
    UINT16 Caps;
    UINT8 SsidLen;
    UINT8 Ssid[IWL_SSID_MAX];
    UINT8 HasRsn;
    UINT8 RatesLen;     /* IE1，最多 8 */
    UINT8 Rates[8];
    UINT8 ExtRatesLen;  /* IE50，最多 8（够用） */
    UINT8 ExtRates[8];
    UINT8 HtLen;        /* IE45 载荷，最多 26；0=无 */
    UINT8 Ht[26];
    UINT8 RsnLen;       /* 整段 IE48（含 id/len），最多 48 */
    UINT8 Rsn[48];
} IWL_BSS;

extern volatile UINT8 *gIwlBar;
extern UINT64 gIwlBarPhys;
extern UINT64 gIwlBarSize;
extern UINT16 gIwlDid;
extern UINT16 gIwlHwRev;
extern UINT8 gIwlBus;
extern UINT8 gIwlDev;
extern UINT8 gIwlFn;
extern UINT8 gIwlMac[6];
extern UINT8 gIwlBssid[6];
extern UINT16 gIwlAid;
extern int gIwlReady;
extern int gIwlFwOk;
extern int gIwlBarOk;
extern int gIwlAlive;
extern int gIwlAssociated;
extern int gIwlWpa2Ok;
extern int gIwlScanCount;
extern int gIwlSsidOk;
extern UINTN gIwlFwSize;
extern char gIwlSsid[IWL_SSID_MAX + 1];
extern char gIwlPsk[IWL_PSK_MAX + 1];
extern UINT8 gIwlPmk[32];
extern int gIwlPmkOk;
extern UINT8 gIwlTxStaId;
/* PR-S-iwl-split-2：数据面一次性黄字开关 */
extern int gDatTxLogged;
extern int gDatRxLogged;
extern int gDtxLogged;
extern int gRxMicLogged;
extern int gRxMicU;
extern int gRxDiscLogged;
extern int gRxOffLogged;
extern int gRxLlcLogged;
extern int gRxStLogged;
void IwlRxDataToNet(UINT8 *Frame, UINTN FLen, UINT32 St);
void IwlLogDataTx(const IWL_RX_PKT *Pkt, UINTN Len);
extern UINT8 gIwlPtk[16];
extern UINT8 gIwlGtk[16];
extern UINT8 gIwlGtkAlt[16]; /* 刀 #182：KdeLen 大时 +16 备选 */
extern UINT8 gIwlGtkId; /* 刀 #179：GTK KDE KeyID（帧头 KeyID=2 常见） */
extern UINT8 gIwlGtkAltOk; /* 1=Alt 有效 */
/* 刀 #174：AssocReq 自建 STA RSN；M2 Key Data 必须同一份 */
extern UINT8 gIwlStaRsn[32];
extern UINT8 gIwlStaRsnLen;
extern IWL_BSS gIwlTarget;
extern IWL_FW_IMG gIwlImgRt;
extern IWL_FW_IMG gIwlImgInit;
extern UINT8 *gIwlFwBlob;
extern UINTN gIwlFwBlobSize;
extern UINT32 gIwlPhyCfg;
extern UINT32 gIwlCalibFlow[];
extern UINT32 gIwlCalibEvent[];
extern UINT32 gIwlSchedBase;

/* MMIO / stall */
UINT32 IwlMmioR32(UINT32 Off);
void IwlMmioW32(UINT32 Off, UINT32 Val);
void IwlMmioSet(UINT32 Off, UINT32 Mask);
void IwlMmioClr(UINT32 Off, UINT32 Mask);
int IwlPollBit(UINT32 Off, UINT32 Bits, UINT32 Mask, UINT32 TimeoutUs);
void IwlStallUs(UINT32 Us);
void IwlStallMs(UINT32 Ms);
void IwlFlushDma(const void *Ptr, UINTN Size);
int IwlNicLock(void);
void IwlNicUnlock(void);
UINT32 IwlPrphR(UINT32 Addr);
void IwlPrphW(UINT32 Addr, UINT32 Val);

int IwlPciFind(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut, UINT16 *DidOut);
int IwlMapBar(UINT64 Bar);
void IwlPciPathPrep(void);
/* 1=ok；-1=prep；-2=apm；0=无 BAR */
int IwlHwStart(void);
int IwlNicInit(void);
int IwlFwTryLoad(void);
/* 1=ok；0=alive=fail；-1=fwload=fail；-2=init_alive=fail */
int IwlFwParseAndLoad(void);
int IwlLoadUcode8000(const IWL_FW_IMG *Img);
int IwlFhAliveVal(UINT32 V);
void IwlFhProbeAccess(const char *Tag);
void IwlFhLogFail(int Why, UINT32 Dst, UINT32 Phys, UINT32 Tssr, UINT32 Tcsr);
int IwlWaitAlive(void);
void IwlRxDrain(void);
/* 1=ok/无分页；0=失败（仍可软失败继续） */
int IwlPagingInit(const IWL_FW_IMG *Img);
/* 刀 #57：INIT 校准捕获 + RT phy_db/PHY_CFG */
int IwlPhyInitCalib(void);
int IwlPhyDbSend(void);
int IwlPhyCfgRt(void);

int IwlRxInit(void);
void IwlRxPoll(void);
int IwlRxTake(IWL_RX_PKT **OutPkt, UINTN *OutLen);
/* cmd sync 等待时暂存 RX_MPDU，供 EAPOL 后取（勿吞 msg1） */
void IwlRxHoldMpdu(const IWL_RX_PKT *Pkt, UINTN Len);
int IwlRxTakeHeld(IWL_RX_PKT **OutPkt, UINTN *OutLen);
UINT8 IwlRxHoldCount(void); /* #130：EAPOL 前看暂存数 */
void IwlRxHoldFlush(void);   /* #176：丢掉 assoc 后堆积的旧 M1 */
void IwlRxRestock(void);
UINT32 IwlRxDiagClosed(void);
UINT32 IwlRxDiagRead(void);

int IwlTxInit(void);
int IwlSendCmd(UINT32 Id, const void *Data, UINT32 Len, int Sync); /* Sync: <0 入队不响铃；0 响铃不等；1=2.5s；>=2 等 Sync ms */
void IwlCmdKick(void);
int IwlCmdqUnwedge(void);
int IwlSendFrameRaw(const UINT8 *Frame80211, UINTN Len);
int IwlRspStashClaim(UINT8 Code, UINT8 Idx);
void IwlCmdqSnap(void);
int IwlPostAlive(void);
int IwlEnableAuxTxq(void);
int IwlPrepareApTxq(void); /* #155：SCD_QUEUE_CFG 之前只绑 q5 环 */
int IwlEnableApTxq(void); /* 标记 q5 可发，不再改调度器额度 */
void IwlLogApQ(void); /* #154：q5 读/写指针 */
int IwlCmdqReset(void);
void IwlAuxTxLogReset(void); /* #126：apsta 后重开黄字窗 */

int IwlCfgLoad(void);
int IwlPmkPrepare(void);
int IwlAddApSta(void);
int IwlPhyCtxtTune(UINT8 Chan);
void IwlProtectSession(void);
void IwlReadHwMac(void);   /* #133：读 WFMP 并黄字；先不改 gIwlMac */
void IwlApplyHwMac(void);  /* #136：MVM/扫描前换上芯片地址 */
int IwlMacCtxtPrep(void);  /* #132：auth 后、assoc 前 mac0+bind+TE */
int IwlMacCtxtAssoc(void); /* assoc 后：apsta+macmod+apqhw */
int IwlStaKeysInstall(void); /* #180：ADD_STA_KEY PTK/GTK + 开固件解密 */
int IwlMvmPostAlive(void);
int IwlScanRun(void);
int IwlAssocRun(void);
int IwlEapolRun(void);

int IwlAesEncrypt(const UINT8 Key[16], const UINT8 In[16], UINT8 Out[16]);
void IwlAesKeyExpand(const UINT8 Key[16], UINT8 Rk[176]);
void IwlAesAddRoundKey(UINT8 S[16], const UINT8 *Rk);
int IwlAesDecrypt(const UINT8 Key[16], const UINT8 In[16], UINT8 Out[16]);
int IwlAesUnwrap(const UINT8 Kek[16], const UINT8 *In, UINTN InLen,
                 UINT8 *Out, UINTN OutCap, UINTN *OutLen);
void IwlAesCmac(const UINT8 Key[16], const UINT8 *Msg, UINTN Len, UINT8 Out[16]);
void IwlSha1(const UINT8 *Data, UINTN Len, UINT8 Out[20]);
void IwlKdfSha256(const UINT8 *Key, UINTN KeyLen, const char *Label,
                  const UINT8 *Ctx, UINTN CtxLen, UINT8 *Out, UINTN OutLen);
void IwlHmacSha1(const UINT8 *Key, UINTN KeyLen, const UINT8 *Data, UINTN Len, UINT8 Out[20]);
int IwlPbkdf2Sha1(const char *Pass, const UINT8 *Salt, UINTN SaltLen,
                  UINT32 Iter, UINT8 *Out, UINTN OutLen);
int IwlCcmpEncrypt(const UINT8 Key[16], UINT64 Pn, UINT8 *Frame, UINTN HdrLen, UINTN BodyLen);
int IwlCcmpDecrypt(const UINT8 Key[16], UINT64 Pn, UINT8 *Frame, UINTN HdrLen, UINTN BodyLen);

#ifndef IWL_LOG_VERBOSE
#define IWL_LOG_VERBOSE 0 /* 1=成功里程碑/FH/rxraw/cmdq 等全开黄字 */
#endif

void IwlLogBound(void);
/* 默认仅异常（fail/miss/soft…）；成功看 Bound；VERBOSE=1 才打里程碑 */
void IwlLogStage(const char *Tag);
void IwlLogVerb(const char *Tag);

/* PR-S-iwl-split-1：IwlEapol* 搬家后的跨文件辅助 */
extern int gIwlTxRsp;
extern int gIwlRxCode;
void IwlEapolZero(void *P, UINTN N);
void IwlEapolCopyN(UINT8 *D, const UINT8 *S, UINTN N);
int IwlEapolMemCmp(const UINT8 *A, const UINT8 *B, UINTN N);
UINTN IwlDot11DataHdrLen(UINT16 Fc);
int IwlFindEapol(UINT8 *Dot11, UINTN FLen, UINT8 **OutEap, UINTN *OutLen);
int IwlAddr1IsUs(const UINT8 *Dot11);
UINT16 IwlBe16(const UINT8 *P);
void IwlPutBe16(UINT8 *P, UINT16 V);
void IwlPrf384(const UINT8 Pmk[32], const UINT8 *A, UINTN Alen,
               const UINT8 *B, UINTN Blen, UINT8 Out[48]);
void IwlBuildPtk(const UINT8 Pmk[32], const UINT8 *Anon, const UINT8 *Snon,
                 UINT8 Ptk[48], UINT8 Ver);
void IwlEapolMic(UINT8 Ver, const UINT8 Kck[16], UINT8 *Eapol,
                 UINTN EapolLen, UINT8 Mic[16]);
void IwlInstallGtk(const UINT8 *Eapol, UINTN EapLen, const UINT8 Kek[16]);
int IwlRxDrainForEapol(UINT8 *EapOut, UINTN *EapLenOut, UINTN Cap,
                       UINT32 *MpduN, UINT32 *DataN, UINT32 *UniN,
                       UINT32 *BeaconN);
int IwlSendEapol(const UINT8 *Eapol, UINTN EapolLen);
int IwlEapolWaitMsg1(UINT8 *Eapol, UINTN *EapLen, UINT8 *Anonce, UINT8 *Replay,
                     UINT8 *KeyDescOut);
int IwlEapolFinishHandshake(UINT8 *Eapol, UINTN EapLen, UINT8 *Anonce, UINT8 *Replay,
                            UINT8 KeyDesc);

#endif /* IWL_PRIVATE_H */
