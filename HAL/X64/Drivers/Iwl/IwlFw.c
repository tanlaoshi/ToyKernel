/*
 * IwlFw.c — 读 FW/IWL8265.UCODE（PR-N-wifi-1）
 *
 * 本刀只确认文件在盘上并记下长度；不解析/不灌芯片（wifi-2）。
 */
#include "IwlPrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "ToySerialLog.h"

UINTN gIwlFwSize;

int IwlFwTryLoad(void) {
    UINT8 Probe[64];
    UINTN Sz = 0;
    int Err;

    gIwlFwOk = 0;
    gIwlFwSize = 0;
    Err = FileSystemReadFile(IWL_FW_PATH, Probe, sizeof(Probe), &Sz);
    if (Err != FAT_OK || Sz == 0) {
        ToyLogDrv("Boot: iwl8265 fw miss (FW/IWL8265.UCODE)\n");
        return 0;
    }
    /*
     * 完整 blob ~2.3MiB；wifi-1 不常驻。有头即认为 present。
     * 精确长度：再读大缓冲不划算；用「可读」+ 日志。
     */
    gIwlFwOk = 1;
    gIwlFwSize = Sz; /* 至少读到的头长；完整灌入另刀 */
    /* 成功不单打黄字；由 IwlLogBound 的 fw=ok 统一报 */
    return 1;
}
