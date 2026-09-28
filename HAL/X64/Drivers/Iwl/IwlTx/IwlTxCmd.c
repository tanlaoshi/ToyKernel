/*
 * IwlTxCmd.c — host command 入队与同步等回（PR-S-iwl-split-2）
 */
#include "IwlTxInternal.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"

int IwlSendCmd(UINT32 Id, const void *Data, UINT32 Len, int Sync) {
    UINT32 Slot;
    UINT32 Seq;
    UINT8 *Buf;
    IWL_TFD *Tfd;
    UINT32 Need;
    UINT32 Wait;
    UINT32 Group = (Id >> 8) & 0xffu;
    UINT32 Opcode = Id & 0xffu;
    UINT32 Version = (Id >> 16) & 0xffu;
    UINT32 HdrLen = Group ? sizeof(IWL_CMD_HDR_WIDE) : sizeof(IWL_CMD_HDR);
    UINT8 LastCode = 0;
    UINT32 RxHits = 0;

    if (!gTxReady || Len > IWL_CMD_PAYLOAD_MAX || HdrLen + Len > PAGE_SIZE) {
        return -1;
    }
    /*
     * Linux：sequence 用 write_ptr 低 8 位（环 256）；cmd 缓冲窗口 32。
     * 以前用 Slot 当 sequence，宽命令回包对不上会误判超时。
     */
    Seq = gCmdWrite & 0xffu;
    Slot = Seq & IWL_CMD_Q_MASK;
    Buf = gCmdBufs[Slot];
    IwlTxZero(Buf, HdrLen + Len);
    if (Group) {
        IWL_CMD_HDR_WIDE *W = (IWL_CMD_HDR_WIDE *)Buf;
        W->Opcode = (UINT8)Opcode;
        W->GroupId = (UINT8)Group;
        W->Idx = (UINT8)Seq;
        W->Qid = (UINT8)IWL_CMD_QUEUE;
        W->Length = (UINT16)Len;
        W->Reserved = 0;
        W->Version = (UINT8)Version;
    } else {
        IWL_CMD_HDR *H = (IWL_CMD_HDR *)Buf;
        H->Code = (UINT8)Opcode;
        H->Flags = 0;
        H->Idx = (UINT8)Seq;
        H->Qid = (UINT8)IWL_CMD_QUEUE;
    }
    if (Data && Len) {
        IwlTxCopy(Buf + HdrLen, Data, Len);
    }
    Need = HdrLen + Len;
    /*
     * 刀 #78：#77 证 SCAN_REQ DMA 完但 cmdto o=0D。刀 #37 注释写
     * 「BC 用真实 Need」，调用却仍传 0→BC=2（仅 CRC+DELIM）。OpenBSD
     * 对 cmd 也传 0，但我们大包（L0634）不 ACK；小包碰巧能过。改回 Need。
     */
    {
        UINT64 Phys = gCmdBufPhys[Slot];
        Tfd = &gCmdTfd[Seq & IWL_TFD_Q_MASK];
        IwlTxZero(Tfd, sizeof(*Tfd));
        IwlTfdSetTb(Tfd, 0, Phys, (UINT16)Need);
        IwlFlushDma(Buf, Need);
        IwlFlushDma(Tfd, sizeof(*Tfd));
    }
    IwlUpdateSched(IWL_CMD_QUEUE, Seq & IWL_TFD_Q_MASK, 0, (UINT16)Need);

    gCmdWrite = (gCmdWrite + 1) & IWL_TFD_Q_MASK;
    /* Sync < 0：只入队不响铃（刀 #56 与下一命令合并 WRPTR） */
    if (Sync >= 0) {
        if (!IwlNicLock()) {
            return -1;
        }
        IwlMmioW32(IWL_HBUS_TARG_WRPTR,
                   (IWL_CMD_QUEUE << 8) | (gCmdWrite & 0xff));
        IwlNicUnlock();
    }

    if (Sync <= 0) {
        return 0;
    }
    /*
     * Sync==1：兼容旧 2.5s。
     * Sync>=2：等 Sync ms（刀 #100：apsta 短等，勿楔死 msg1 窗）。
     */
    {
        UINT32 WaitMax = (Sync == 1) ? 2500u : (UINT32)Sync;
        if (WaitMax > 5000u) {
            WaitMax = 5000u;
        }
        for (Wait = 0; Wait < WaitMax; Wait++) {
        IWL_RX_PKT *Pkt;
        UINTN RLen;
        int Took = 0;

        IwlRxPoll();
        /* 刀 #145：不封顶时 beacon 灌满，Wait 永不加，te=ok 后无黄字 */
        while (Took < 24 && IwlRxTake(&Pkt, &RLen)) {
            UINT8 Ridx = Pkt->Hdr.Idx;
            UINT8 Rqid = Pkt->Hdr.Qid;
            UINT8 Code = Pkt->Hdr.Code;
            const UINT8 *Pay;
            Took++;
            LastCode = Code;
            RxHits++;
            if (Code == 0x02u && RLen >= sizeof(IWL_CMD_HDR) + 4 + 8) {
                UINT16 BadSeq;
                Pay = Pkt->Data;
                BadSeq = (UINT16)Pay[6] | ((UINT16)Pay[7] << 8);
                if (Ridx == (UINT8)Seq || BadSeq == (UINT16)Seq) {
                    char Line[56];
                    char Hex[12];
                    int n = 0;
                    const char *P = "cmderr e=";
                    UINT32 Et = (UINT32)Pay[0] | ((UINT32)Pay[1] << 8)
                              | ((UINT32)Pay[2] << 16) | ((UINT32)Pay[3] << 24);
                    while (*P) {
                        Line[n++] = *P++;
                    }
                    HalSerialFormatHex(Hex, Et, 8);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n++] = Hex[4];
                    Line[n++] = Hex[5];
                    Line[n++] = Hex[6];
                    Line[n++] = Hex[7];
                    Line[n++] = Hex[8];
                    Line[n++] = Hex[9];
                    Line[n++] = ' ';
                    Line[n++] = 'o';
                    Line[n++] = '=';
                    HalSerialFormatHex(Hex, Pay[4], 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n++] = ' ';
                    Line[n++] = 'w';
                    Line[n++] = '=';
                    HalSerialFormatHex(Hex, Opcode, 2);
                    Line[n++] = Hex[2];
                    Line[n++] = Hex[3];
                    Line[n] = 0;
                    IwlLogStage(Line);
                    IwlLogCmdq(Seq);
                    gCmdRead = (gCmdRead + 1) & IWL_CMD_Q_MASK;
                    return -1;
                }
            }
            /*
             * OpenBSD cmd_done：qid bit7=0 且 idx 对上即认。
             * REPLY_ERROR(0x02) 另计；其余一律成功（空 ACK 的 Code 即 opcode）。
             */
            if (!(Rqid & 0x80u) && Ridx == (UINT8)Seq) {
                if (Code == 0x02u) {
                    continue;
                }
                gCmdRead = (gCmdRead + 1) & IWL_CMD_Q_MASK;
                return 0;
            }
            /* 刀 #98：同步等命令时勿丢 RX_MPDU（assoc 后 msg1 会来） */
            if (Code == IWL_RX_MPDU_CMD) {
                IwlRxHoldMpdu(Pkt, RLen);
                continue;
            }
            /* 刀 #45：host 回包（非 bit7）错序则暂存，供 scfg kick 认领 */
            if (!(Rqid & 0x80u) && Code != 0x02u) {
                gRspStash = 1;
                gRspStashCode = Code;
                gRspStashIdx = Ridx;
                gRspStashQid = Rqid;
            }
            if (RxHits <= 3u) {
                char Line[40];
                char Hex[12];
                int n = 0;
                const char *P = "rxmiss c=";
                while (*P) {
                    Line[n++] = *P++;
                }
                HalSerialFormatHex(Hex, Code, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'i';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Ridx, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 'q';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Rqid, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n++] = ' ';
                Line[n++] = 's';
                Line[n++] = '=';
                HalSerialFormatHex(Hex, Seq & 0xffu, 2);
                Line[n++] = Hex[2];
                Line[n++] = Hex[3];
                Line[n] = 0;
                IwlLogVerb(Line);
            }
        }
        IwlStallMs(1);
    }
    } /* WaitMax */
    gCmdRead = (gCmdRead + 1) & IWL_CMD_Q_MASK;
    {
        char Line[48];
        char Hex[12];
        int n = 0;
        const char *P = "cmdto n=";
        while (*P) {
            Line[n++] = *P++;
        }
        HalSerialFormatHex(Hex, RxHits & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'c';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, LastCode, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'o';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, Opcode, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = ' ';
        Line[n++] = 'r';
        Line[n++] = '=';
        HalSerialFormatHex(Hex, IwlRxDiagClosed() & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n++] = '/';
        HalSerialFormatHex(Hex, IwlRxDiagRead() & 0xffu, 2);
        Line[n++] = Hex[2];
        Line[n++] = Hex[3];
        Line[n] = 0;
        IwlLogStage(Line);
        IwlLogCmdq(Seq);
    }
    return -1;
}
