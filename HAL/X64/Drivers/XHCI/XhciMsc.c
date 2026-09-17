/*
 * XhciMsc.c — PR-H-xhci-msc-split-1：MSC Bulk / claim / BOT / capacity
 *
 * 从 XhciCore.c 原样搬家；不改语义。事件环 Bulk 完成仍在 XhciCore.c。
 */
#include "XHCI/XhciInternal.h"

/* MSC 全局仍定义在 XhciCore.c（BSS 顺序影响 HID DMA 环地址；勿迁出） */

static int gMscInSense;  /* REQUEST SENSE 重入保护 */

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

static int WaitBulk(void) {
    int Own = 0;
    int Result = -1;

    if (!XhciEventIsExclusive()) {
        XhciEventEnterExclusive();
        Own = 1;
    }

    if (!HalCpuIsHypervisor()) {
        UINT64 T0 = ReadTsc();
        UINT64 Need = 500ULL * 3000000ULL; /* ~500ms：大 U 盘 INQUIRY 可慢 */

        for (;;) {
            ProcessEventsRealPc();
            ServiceHidCompletions();
            if (gBulkDone) {
                Result = (gBulkCode == CC_SUCCESS || gBulkCode == CC_SHORT_PACKET) ? 0
                                                                                : -1;
                break;
            }
            if (ReadTsc() - T0 >= Need) {
                break;
            }
            __asm__ volatile ("pause");
        }
    } else {
        int Timeout = 200000;

        while (Timeout--) {
            ProcessEvents();
            ServiceHidCompletions();
            if (gBulkDone) {
                Result = (gBulkCode == CC_SUCCESS || gBulkCode == CC_SHORT_PACKET) ? 0
                                                                                : -1;
                break;
            }
        }
    }

    if (Own) {
        XhciEventLeaveExclusive();
    }
    return Result;
}

/*
 * PR-H-msc-5：Bulk 普通传输。DirIn=1 → Bulk IN 环；0 → Bulk OUT。
 * 成功 0；失败 -1。短包算成功（CSW/INQUIRY 常见）。
 */
int XhciBulkXfer(int DirIn, void *Buf, UINT32 Len) {
    XHCI_TRB *Ring;
    RING_STATE *St;
    UINT32 Dci;
    UINT32 Ctrl;

    if (!gMscClaimed || gMscScanSlot == 0 || Buf == 0 || Len == 0) {
        return -1;
    }
    if (gMscBulkInDci == 0 || gMscBulkOutDci == 0) {
        return -1;
    }

    if (DirIn) {
        Ring = gBulkInRing;
        St = &gBulkIn;
        Dci = gMscBulkInDci;
        Ctrl = TRB_TYPE(TRB_NORMAL) | TRB_IOC | TRB_ISP;
    } else {
        Ring = gBulkOutRing;
        St = &gBulkOut;
        Dci = gMscBulkOutDci;
        Ctrl = TRB_TYPE(TRB_NORMAL) | TRB_IOC;
    }

    FlushDma(Buf, Len);
    gBulkDone = 0;
    gBulkCode = 0;
    gBulkRemain = 0;
    /* excl-1：门铃与 WaitBulk 同独占窗 */
    XhciEventEnterExclusive();
    Enqueue(Ring, St, PointerToPhysical(Buf), Len, Ctrl);
    FlushDma(Ring, sizeof(XHCI_TRB) * (St->Size ? St->Size : RING_SIZE));
    RingDoorbell(gMscScanSlot, Dci);
    if (WaitBulk() < 0) {
        XhciEventLeaveExclusive();
        BootLogHex("Boot: MSC bulk fail cc=", gBulkCode, 2);
        return -1;
    }
    XhciEventLeaveExclusive();
    FlushDma(Buf, Len);
    return 0;
}

/*
 * BOT：CBW → [DATA] → CSW。DataIn=1 时数据走 Bulk IN。
 * 成功 0；失败 -1。CSW≠0 时发 REQUEST SENSE 清粘滞 sense（不少棒必须）。
 */
static int MscBot(UINT8 *CbwCb, UINT8 CbLen, UINT32 DataLen, int DataIn,
                  void *Data) {
    UINT8 Cbw[32] __attribute__((aligned(64)));
    UINT8 Csw[16] __attribute__((aligned(64)));
    static UINT32 Tag;
    UINT32 i;

    if (!gMscClaimed) {
        return -1;
    }
    if (CbLen == 0 || CbLen > 16) {
        return -1;
    }
    if (DataLen != 0 && Data == 0) {
        return -1;
    }

    Tag++;
    ZeroMemory(Cbw, sizeof(Cbw));
    Cbw[0] = 0x55;
    Cbw[1] = 0x53;
    Cbw[2] = 0x42;
    Cbw[3] = 0x43; /* USBC */
    Cbw[4] = (UINT8)(Tag);
    Cbw[5] = (UINT8)(Tag >> 8);
    Cbw[6] = (UINT8)(Tag >> 16);
    Cbw[7] = (UINT8)(Tag >> 24);
    Cbw[8] = (UINT8)(DataLen);
    Cbw[9] = (UINT8)(DataLen >> 8);
    Cbw[10] = (UINT8)(DataLen >> 16);
    Cbw[11] = (UINT8)(DataLen >> 24);
    Cbw[12] = DataIn ? 0x80u : 0x00u;
    Cbw[13] = 0; /* LUN */
    Cbw[14] = CbLen;
    for (i = 0; i < CbLen; i++) {
        Cbw[15 + i] = CbwCb[i];
    }

    if (XhciBulkXfer(0, Cbw, 31) < 0) {
        BootLog("Boot: MSC bot cbw fail\n");
        return -1;
    }
    if (DataLen != 0) {
        if (XhciBulkXfer(DataIn ? 1 : 0, Data, DataLen) < 0) {
            BootLog("Boot: MSC bot data fail\n");
            return -1;
        }
    }
    ZeroMemory(Csw, sizeof(Csw));
    if (XhciBulkXfer(1, Csw, 13) < 0) {
        BootLog("Boot: MSC bot csw fail\n");
        return -1;
    }
    /* USBS */
    if (Csw[0] != 0x55 || Csw[1] != 0x53 || Csw[2] != 0x42 || Csw[3] != 0x53) {
        BootLog("Boot: MSC bot csw sig\n");
        return -1;
    }
    if (Csw[12] != 0) {
        if (!gMscInSense) {
            UINT8 Sense[18] __attribute__((aligned(64)));
            UINT8 Scdb[16];

            BootLogHex("Boot: MSC bot status=", Csw[12], 2);
            gMscInSense = 1;
            ZeroMemory(Scdb, sizeof(Scdb));
            Scdb[0] = 0x03; /* REQUEST SENSE */
            Scdb[4] = 18;
            ZeroMemory(Sense, sizeof(Sense));
            if (MscBot(Scdb, 6, 18, 1, Sense) == 0) {
                BootLogHex("Boot: MSC sense key=", Sense[2] & 0x0Fu, 2);
                BootLogHex("Boot: MSC sense asc=", Sense[12], 2);
                BootLogHex("Boot: MSC sense ascq=", Sense[13], 2);
            }
            gMscInSense = 0;
        }
        return -1;
    }
    return 0;
}

/*
 * PR-H-msc-5：INQUIRY + READ CAPACITY(10)。不读分区、不挂 FAT。
 * 成功 0 并填 gMscBlockCount/Size；失败 -1。
 */
int XhciMscCapacity(void) {
    UINT8 Inquiry[36] __attribute__((aligned(64)));
    UINT8 Cap[8] __attribute__((aligned(64)));
    UINT8 Cdb[16];
    UINT32 LastLba;
    UINT32 Bsz;

    if (!gMscClaimed || gMscScanSlot == 0) {
        BootLog("Boot: MSC capacity not claimed\n");
        return -1;
    }

    gMscCapacityOk = 0;
    gMscBlockCount = 0;
    gMscBlockSize = 0;

    ZeroMemory(Cdb, sizeof(Cdb));
    Cdb[0] = 0x12; /* INQUIRY */
    Cdb[4] = 36;
    ZeroMemory(Inquiry, sizeof(Inquiry));
    if (MscBot(Cdb, 6, 36, 1, Inquiry) < 0) {
        BootLog("Boot: MSC inquiry fail\n");
        return -1;
    }
    BootLogHex("Boot: MSC inquiry pdt=", Inquiry[0] & 0x1Fu, 2);
    {
        char Line[48];
        int n = 0;
        const char *P = "Boot: MSC vendor=";
        int i;

        while (*P && n < 20) {
            Line[n++] = *P++;
        }
        for (i = 8; i < 16 && n < 46; i++) {
            char C = (char)Inquiry[i];

            if (C < 32 || C > 126) {
                C = '.';
            }
            Line[n++] = C;
        }
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }

    ZeroMemory(Cdb, sizeof(Cdb));
    Cdb[0] = 0x25; /* READ CAPACITY(10) */
    ZeroMemory(Cap, sizeof(Cap));
    if (MscBot(Cdb, 10, 8, 1, Cap) < 0) {
        BootLog("Boot: MSC readcap fail\n");
        return -1;
    }
    LastLba = ((UINT32)Cap[0] << 24) | ((UINT32)Cap[1] << 16) |
              ((UINT32)Cap[2] << 8) | (UINT32)Cap[3];
    Bsz = ((UINT32)Cap[4] << 24) | ((UINT32)Cap[5] << 16) |
          ((UINT32)Cap[6] << 8) | (UINT32)Cap[7];
    if (Bsz == 0) {
        BootLog("Boot: MSC readcap bsz0\n");
        return -1;
    }
    gMscBlockCount = LastLba + 1u;
    gMscBlockSize = Bsz;
    gMscCapacityOk = 1;
    BootLogHex("Boot: MSC blocks=", gMscBlockCount, 8);
    BootLogHex("Boot: MSC bsize=", gMscBlockSize, 8);
    return 0;
}

UINT32 XhciMscBlockCount(void) {
    return gMscCapacityOk ? gMscBlockCount : 0;
}

UINT32 XhciMscBlockSize(void) {
    return gMscCapacityOk ? gMscBlockSize : 0;
}

/*
 * PR-H-msc-6：逐扇区 BOT READ(10)/WRITE(10)。
 * 仅支持逻辑块 512；bounce 对齐。勿带 FUA/SYNC：不少棒 CSW=1 且会搞丢写缓存。
 */
static int MscXferSectors(UINT32 Lba, UINT32 Count, void *Buffer, int Write) {
    UINT8 Bounce[512] __attribute__((aligned(64)));
    UINT8 *Ptr = (UINT8 *)Buffer;
    UINT32 i;

    if (!gMscCapacityOk || gMscBlockSize != 512 || Buffer == 0 || Count == 0) {
        return 0;
    }
    if (Lba >= gMscBlockCount || Count > gMscBlockCount - Lba) {
        return 0;
    }

    for (i = 0; i < Count; i++) {
        UINT8 Cdb[16];
        UINT32 Cur = Lba + i;

        ZeroMemory(Cdb, sizeof(Cdb));
        Cdb[0] = Write ? 0x2Au : 0x28u; /* WRITE(10) / READ(10) */
        Cdb[2] = (UINT8)(Cur >> 24);
        Cdb[3] = (UINT8)(Cur >> 16);
        Cdb[4] = (UINT8)(Cur >> 8);
        Cdb[5] = (UINT8)(Cur);
        Cdb[7] = 0;
        Cdb[8] = 1;
        if (Write) {
            CopyMemory(Bounce, Ptr + (UINTN)i * 512u, 512);
            if (MscBot(Cdb, 10, 512, 0, Bounce) < 0) {
                return 0;
            }
        } else {
            ZeroMemory(Bounce, sizeof(Bounce));
            if (MscBot(Cdb, 10, 512, 1, Bounce) < 0) {
                return 0;
            }
            CopyMemory(Ptr + (UINTN)i * 512u, Bounce, 512);
        }
    }
    return 1;
}

int XhciMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    return MscXferSectors(Lba, Count, Buffer, 0);
}

int XhciMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    return MscXferSectors(Lba, Count, (void *)Buffer, 1);
}

/*
 * USB MSC：多数棒不支持 SYNCHRONIZE CACHE；失败 CSW 后部分控制器会丢未刷写缓存，
 * 表现为目录项「写成功」但读回旧内容 → store remove file remains。
 * Flush 改为空操作；依赖 WRITE(10) 本身落盘。
 */
int XhciMscFlush(void) {
    return 1;
}

/* 配置描述符中找 MSC Bulk IN/OUT（偏好 BOT；允许 UASP；兜底任一对 Bulk） */
static int ParseMscBulk(UINT8 *Cfg, UINT16 Total, UINT8 *OutIface,
                       UINT8 *EpIn, UINT16 *MpsIn, UINT8 *EpOut, UINT16 *MpsOut) {
    UINT16 Off = 0;
    UINT8 CurIface = 0xFF;
    UINT8 CurAlt = 0;
    UINT8 IfaceClass = 0;
    UINT8 IfaceSub = 0;
    UINT8 IfaceProto = 0;
    UINT8 BestIface = 0xFF;
    UINT8 BestIn = 0;
    UINT8 BestOut = 0;
    UINT16 BestInMps = 0;
    UINT16 BestOutMps = 0;
    int BestScore = -1;

    while (Off + 2 <= Total) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            CurIface = Cfg[Off + 2];
            CurAlt = Cfg[Off + 3];
            IfaceClass = Cfg[Off + 5];
            IfaceSub = Cfg[Off + 6];
            IfaceProto = Cfg[Off + 7];
        } else if (Type == 5 && Len >= 7 && CurIface != 0xFF && CurAlt == 0) {
            UINT8 Addr = Cfg[Off + 2];
            UINT8 Attr = Cfg[Off + 3];
            UINT16 Mps = (UINT16)(Cfg[Off + 4] | (Cfg[Off + 5] << 8));
            int Score;

            if ((Attr & 0x03) != 2) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            /* class8 BOT/UASP 优先；其它 iface 仅作兜底（score 0） */
            if (IfaceClass == 0x08) {
                Score = 10;
                if (IfaceSub == 0x06) {
                    Score += 2;
                }
                if (IfaceProto == 0x50 || IfaceProto == 0x62) {
                    Score += 4;
                }
            } else {
                Score = 0;
            }
            if (Score < BestScore) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Score > BestScore) {
                BestScore = Score;
                BestIface = CurIface;
                BestIn = 0;
                BestOut = 0;
                BestInMps = 0;
                BestOutMps = 0;
            } else if (CurIface != BestIface) {
                Off = (UINT16)(Off + Len);
                continue;
            }
            if (Addr & 0x80) {
                BestIn = Addr;
                BestInMps = Mps ? Mps : 64;
            } else {
                BestOut = Addr;
                BestOutMps = Mps ? Mps : 64;
            }
        }
        Off = (UINT16)(Off + Len);
    }

    /* 兜底 score=0 须成对 Bulk；class8 同 */
    if (BestIn == 0 || BestOut == 0) {
        return 0;
    }
    if (BestScore < 0) {
        return 0;
    }
    /* 无 class8 时仅当找到成对 Bulk 才接受（score 0） */
    if (OutIface) {
        *OutIface = BestIface;
    }
    if (EpIn) {
        *EpIn = BestIn;
    }
    if (MpsIn) {
        *MpsIn = BestInMps;
    }
    if (EpOut) {
        *EpOut = BestOut;
    }
    if (MpsOut) {
        *MpsOut = BestOutMps;
    }
    return 1;
}

static void LogMscCfgIfaces(UINT8 *Cfg, UINT16 Total) {
    UINT16 Off = 0;
    int N = 0;

    while (Off + 9 <= Total && N < 6) {
        UINT8 Len = Cfg[Off];
        UINT8 Type = Cfg[Off + 1];

        if (Len < 2 || Off + Len > Total) {
            break;
        }
        if (Type == 4 && Len >= 9) {
            BootLogHex("Boot: MSC claim iface=", Cfg[Off + 2], 2);
            BootLogHex("Boot: MSC claim iclass=", Cfg[Off + 5], 2);
            BootLogHex("Boot: MSC claim isub=", Cfg[Off + 6], 2);
            BootLogHex("Boot: MSC claim iproto=", Cfg[Off + 7], 2);
            N++;
        }
        Off = (UINT16)(Off + Len);
    }
}

/* ConfigEP：Bulk IN + Bulk OUT（EP Type 6/2）；带回 Route/TT */
static int ConfigureMscBulk(UINT32 SlotId, UINT32 RootPort, UINT8 Speed,
                            UINT8 EpIn, UINT16 MpsIn, UINT8 EpOut, UINT16 MpsOut) {
    UINT8 InNum = EpIn & 0x0F;
    UINT8 OutNum = EpOut & 0x0F;
    UINT32 InDci = (UINT32)InNum * 2 + 1;
    UINT32 OutDci = (UINT32)OutNum * 2 + 0;
    UINT32 CtxEntries = InDci > OutDci ? InDci : OutDci;
    UINT32 *Slot;
    UINT32 *Ep;
    UINT64 Deq;

    if (MpsIn == 0 || MpsIn > 1024) {
        MpsIn = 512;
    }
    if (MpsOut == 0 || MpsOut > 1024) {
        MpsOut = 512;
    }

    ZeroMemory(gInCtx, sizeof(gInCtx));
    *(UINT32 *)(void *)(gInCtx + 4) = (1u << 0) | (1u << InDci) | (1u << OutDci);

    Slot = (UINT32 *)(void *)InSlot();
    Slot[0] = (CtxEntries << 27) | ((UINT32)Speed << 20) | (gMscRoute & 0xFFFFFu);
    Slot[1] = (UINT32)RootPort << 16;
    if (gMscHubSlot != 0 && Speed < 3) {
        Slot[2] = (UINT32)gMscHubSlot | ((UINT32)gMscTtPort << 8);
    }

    if (!gMscBulkRingsInited) {
        (void)XhciMscBringUp();
    }
    InitRing(gBulkInRing, &gBulkIn, RING_SIZE);
    InitRing(gBulkOutRing, &gBulkOut, RING_SIZE);

    Ep = (UINT32 *)(void *)InEp(InDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (6u << 3) | ((UINT32)MpsIn << 16); /* Bulk IN */
    Deq = PointerToPhysical(gBulkInRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsIn;

    Ep = (UINT32 *)(void *)InEp(OutDci);
    Ep[0] = 0;
    Ep[1] = (3u << 1) | (2u << 3) | ((UINT32)MpsOut << 16); /* Bulk OUT */
    Deq = PointerToPhysical(gBulkOutRing) | 1;
    Ep[2] = (UINT32)Deq;
    Ep[3] = (UINT32)(Deq >> 32);
    Ep[4] = (UINT32)MpsOut;

    FlushDma(gInCtx, sizeof(gInCtx));
    FlushDma(gBulkInRing, sizeof(gBulkInRing));
    FlushDma(gBulkOutRing, sizeof(gBulkOutRing));
    FlushDma(gMscScanDevCtx, sizeof(gMscScanDevCtx));

    if (Command(PointerToPhysical(gInCtx), TRB_TYPE(TRB_CONFIG_EP) | TRB_SLOT(SlotId), 0) < 0) {
        BootLog("Boot: MSC claim cfg ep fail\n");
        return 0;
    }

    gMscBulkInDci = InDci;
    gMscBulkOutDci = OutDci;
    gMscBulkInMps = MpsIn;
    gMscBulkOutMps = MpsOut;
    return 1;
}

/*
 * gMscScanSlot 已 Address：取配置、Parse Bulk、SetConfig、ConfigEP。
 * 成功置 gMscClaimed；失败 Disable slot。
 */
int XhciMscFinishClaim(UINT32 RootPort, UINT8 Speed) {
    UINT8 EpIn = 0;
    UINT8 EpOut = 0;
    UINT8 Iface = 0;
    UINT16 MpsIn = 64;
    UINT16 MpsOut = 64;
    UINT16 Total;
    UINT8 ConfigVal = 1;
    UINT8 DevClass;

    if (gMscScanSlot == 0) {
        return 0;
    }
    gXferSlot = gMscScanSlot;
    if (GetDeviceDesc() < 0) {
        BootLog("Boot: MSC claim desc fail\n");
        goto fail;
    }
    DevClass = gCtrlBuf[4];
    BootLogHex("Boot: MSC claim dclass=", DevClass, 2);
    if (DevClass == 0x09) {
        BootLog("Boot: MSC claim skip hub device\n");
        goto fail;
    }
    if (DevClass == 0x03 || DevClass == 0xE0) {
        BootLogHex("Boot: MSC claim skip class=", DevClass, 2);
        goto fail;
    }

    if (GetDesc(0x0200, 0, 9, gMscCfgBuf) < 0) {
        BootLog("Boot: MSC claim cfg9 fail\n");
        goto fail;
    }
    Total = (UINT16)(gMscCfgBuf[2] | (gMscCfgBuf[3] << 8));
    if (Total < 9) {
        Total = 9;
    }
    if (Total > sizeof(gMscCfgBuf)) {
        BootLogHex("Boot: MSC claim cfg trunc want=", Total, 4);
        Total = (UINT16)sizeof(gMscCfgBuf);
    }
    ConfigVal = gMscCfgBuf[5] ? gMscCfgBuf[5] : 1;
    if (GetDesc(0x0200, 0, Total, gMscCfgBuf) < 0) {
        RecoverEp0(gMscScanSlot);
        gXferSlot = gMscScanSlot;
        if (GetDesc(0x0200, 0, Total, gMscCfgBuf) < 0) {
            BootLog("Boot: MSC claim cfg fail\n");
            goto fail;
        }
    }
    BootLogHex("Boot: MSC claim cfg len=", Total, 4);

    if (ConfigHasHubIface(gMscCfgBuf, Total)) {
        /* 根口应在 ClaimPorts 已认领；子口嵌套 hub 跳过 */
        BootLogHex("Boot: MSC claim cfg hub iface port=", RootPort, 2);
        goto fail;
    }

    if (!ParseMscBulk(gMscCfgBuf, Total, &Iface, &EpIn, &MpsIn, &EpOut, &MpsOut)) {
        BootLogHex("Boot: MSC claim no bulk port=", RootPort, 2);
        LogMscCfgIfaces(gMscCfgBuf, Total);
        goto fail;
    }

    if (SetConfig(ConfigVal) < 0) {
        BootLog("Boot: MSC claim setcfg fail\n");
        goto fail;
    }

    if (!ConfigureMscBulk(gMscScanSlot, RootPort, Speed, EpIn, MpsIn, EpOut, MpsOut)) {
        goto fail;
    }

    gMscPort = RootPort;
    gMscClaimed = 1;
    BootLogHex("Boot: MSC claim ok port=", RootPort, 2);
    BootLogHex("Boot: MSC claim slot=", gMscScanSlot, 2);
    BootLogHex("Boot: MSC claim iface=", Iface, 2);
    BootLogHex("Boot: MSC claim epin=", EpIn, 2);
    BootLogHex("Boot: MSC claim epout=", EpOut, 2);
    BootLogHex("Boot: MSC claim route=", gMscRoute, 2);
    BootLog("Boot: MSC claim bulk ok\n");
    return 1;

fail:
    if (gMscScanSlot != 0) {
        gPortNeedForcePr |= (1u << RootPort);
        DisableSlot(gMscScanSlot);
        gMscScanSlot = 0;
    }
    return 0;
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
        int AddrOk;
        UINT32 QuietSave;

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
        Force = 0;
        if (HalCpuIsHypervisor() && (Ps & PORTSC_PED) && (Ps & PORTSC_CCS)) {
            BootLogHex("Boot: MSC claim try PED port=", P, 2);
        } else {
            int Attempt;
            int Ready = 0;

            Force = 1;
            BootLogHex("Boot: MSC claim reset force port=", P, 2);
            for (Attempt = 0; Attempt < 3 && !Ready; Attempt++) {
                if (Attempt > 0) {
                    int W;
                    BootLogHex("Boot: MSC claim Force retry port=", P, 2);
                    /* 丢 CCS 后等设备重新出现（Force 过猛常见） */
                    for (W = 0; W < 50; W++) {
                        Ps = ReadMmio32(gOperationalBase + PortReg(P));
                        if (Ps & PORTSC_CCS) {
                            break;
                        }
                        StallMs(20);
                    }
                }
                if (!ResetPortEx(P, 1)) {
                    Ps = ReadMmio32(gOperationalBase + PortReg(P));
                    if ((Ps & PORTSC_CCS) && !(Ps & PORTSC_PED)) {
                        int W;
                        /* PRC 已到但 PED 慢：再等一会，勿立刻放弃 */
                        for (W = 0; W < 50; W++) {
                            StallMs(20);
                            Ps = ReadMmio32(gOperationalBase + PortReg(P));
                            if ((Ps & PORTSC_PED) && (Ps & PORTSC_CCS)) {
                                Ready = 1;
                                BootLogHex("Boot: MSC claim late PED port=", P, 2);
                                break;
                            }
                            if (!(Ps & PORTSC_CCS)) {
                                break;
                            }
                        }
                    }
                    continue;
                }
                Ready = 1;
                if (!HalCpuIsHypervisor()) {
                    StallMs(100);
                }
            }
            if (!Ready) {
                BootLogHex("Boot: MSC claim reset fail port=", P, 2);
                continue;
            }
        }
        Ps = ReadMmio32(gOperationalBase + PortReg(P));
        if (!(Ps & PORTSC_PED) || !(Ps & PORTSC_CCS)) {
            BootLogHex("Boot: MSC claim not PED port=", P, 2);
            continue;
        }

        Speed = PortSpeed(Ps);
        QuietSave = gDiagQuiet;
        gDiagQuiet = 1;
        AddrOk = AddressDeviceOnPort(P, Speed, &gMscScanSlot, gMscScanDevCtx, 0, 0, 0, 0,
                                     0);
        if (!AddrOk && gMscScanSlot != 0) {
            DisableSlot(gMscScanSlot);
            gMscScanSlot = 0;
        }
        /* QEMU PED 直 Address 失败：Force 再试（真机本轮已 Force） */
        if (!AddrOk && !Force) {
            if (gXhciCmdSick) {
                gDiagQuiet = QuietSave;
                BootLog("Boot: MSC claim abort (cmd sick after PED Address)\n");
                RecoverCommandRing();
                gXhciCmdSick = 0;
                break;
            }
            BootLogHex("Boot: MSC claim addr retry Force port=", P, 2);
            if (ResetPortEx(P, 1)) {
                if (!HalCpuIsHypervisor()) {
                    StallMs(100);
                }
                Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(P)));
                AddrOk = AddressDeviceOnPort(P, Speed, &gMscScanSlot, gMscScanDevCtx,
                                             0, 0, 0, 0, 0);
                if (!AddrOk && gMscScanSlot != 0) {
                    DisableSlot(gMscScanSlot);
                    gMscScanSlot = 0;
                }
            }
        }
        /* 真机 Address 仍失败（cc=0x04/0x11）：再 Force+Address 一轮 */
        if (!AddrOk && Force && !HalCpuIsHypervisor() &&
            (gCmdCode == 4 || gCmdCode == 0x11) && !gXhciCmdSick) {
            BootLogHex("Boot: MSC claim addr 2nd Force port=", P, 2);
            if (gMscScanSlot != 0) {
                DisableSlot(gMscScanSlot);
                gMscScanSlot = 0;
            }
            StallMs(50);
            if (ResetPortEx(P, 1)) {
                StallMs(150);
                Speed = PortSpeed(ReadMmio32(gOperationalBase + PortReg(P)));
                AddrOk = AddressDeviceOnPort(P, Speed, &gMscScanSlot, gMscScanDevCtx,
                                             0, 0, 0, 0, 0);
                if (!AddrOk && gMscScanSlot != 0) {
                    DisableSlot(gMscScanSlot);
                    gMscScanSlot = 0;
                }
            }
        }
        gDiagQuiet = QuietSave;
        if (!AddrOk) {
            BootLogHex("Boot: MSC claim addr fail port=", P, 2);
            BootLogHex("Boot: MSC claim addr cc=", gCmdCode, 2);
            gPortNeedForcePr |= (1u << P);
            if (gXhciCmdSick) {
                BootLog("Boot: MSC claim abort (cmd sick)\n");
                break;
            }
            continue;
        }
        if (gMscScanSlot <= DCBAA_SLOTS) {
            gSlotEp0UsesKbdRing[gMscScanSlot] = 0;
        }
        gPortNeedForcePr &= ~(1u << P);
        gMscRoute = 0;
        gMscHubSlot = 0;
        gMscTtPort = 0;

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
        {
            int Hubish = IsHubDeviceDesc() || (gCtrlBuf[4] == 0x09);

            if (!Hubish && gCtrlBuf[4] == 0) {
                UINT16 Total;

                if (GetDesc(0x0200, 0, 9, gMscCfgBuf) == 0) {
                    Total = (UINT16)(gMscCfgBuf[2] | (gMscCfgBuf[3] << 8));
                    if (Total < 9) {
                        Total = 9;
                    }
                    if (Total > sizeof(gMscCfgBuf)) {
                        Total = (UINT16)sizeof(gMscCfgBuf);
                    }
                    if (GetDesc(0x0200, 0, Total, gMscCfgBuf) == 0 &&
                        ConfigHasHubIface(gMscCfgBuf, Total)) {
                        Hubish = 1;
                        BootLogHex("Boot: MSC claim hub iface root=", P, 2);
                    }
                } else {
                    RecoverEp0(gMscScanSlot);
                    gXferSlot = gMscScanSlot;
                }
            }

            if (Hubish) {
                UINT32 Was = gMscScanSlot;
                UINT32 HubBefore = gHubSlotId;

                gMscScanSlot = 0;
                BootLogHex("Boot: MSC claim hub on root=", P, 2);
                /*
                 * 已有 HID hub 时 ClaimHubOnRootPort 会 DisableSlot(Was)，
                 * 正是外接第二 hub（U 盘所在）→ none + 长时间 Stall 像卡死。
                 */
                if (HubBefore != 0 && Was != 0 && Was != HubBefore) {
                    if (ProbeSecondHubForMsc(Was, P, Speed)) {
                        Ok = 1;
                        goto done;
                    }
                    continue;
                }
                if (ClaimHubOnRootPort(P, Speed, Was)) {
                    if (!HalCpuIsHypervisor()) {
                        StallMs(150);
                    }
                    if (EnumHubChildrenForMsc()) {
                        Ok = 1;
                        goto done;
                    }
                    /* 空 hub：再扫一轮后再决定是否释放（U 盘上电慢） */
                    if (!HalCpuIsHypervisor()) {
                        StallMs(300);
                        if (EnumHubChildrenForMsc()) {
                            Ok = 1;
                            goto done;
                        }
                    }
                    /* HID 未占用此 hub：无 MSC 则放掉，试其它根口 */
                    if (HubBefore == 0 && gHubSlotId != 0 && gHubRootPort == P) {
                        DisableSlot(gHubSlotId);
                        gHubSlotId = 0;
                        gHubRootPort = 0;
                        BootLog("Boot: MSC claim hub no msc, release\n");
                    }
                } else if (Was != 0) {
                    DisableSlot(Was);
                }
                continue;
            }
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
