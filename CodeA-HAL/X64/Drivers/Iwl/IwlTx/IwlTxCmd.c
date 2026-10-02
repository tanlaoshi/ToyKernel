/*
 * IwlTxCmd.c — host command 入队与同步等回编排（PR-F-iwl-3）
 * 等回 / 超时见 IwlTxCmdWait.c
 */
#include "IwlTxInternal.h"
#include "PhysicalMemory.h"

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
            int Rc = IwlSendCmdPollOnce(Seq, Opcode, &LastCode, &RxHits);
            if (Rc == 1) {
                return 0;
            }
            if (Rc < 0) {
                return -1;
            }
            IwlStallMs(1);
        }
    }
    gCmdRead = (gCmdRead + 1) & IWL_CMD_Q_MASK;
    IwlSendCmdLogTimeout(Seq, Opcode, LastCode, RxHits);
    return -1;
}
