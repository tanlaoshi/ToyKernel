/*
 * WifiFw.c — 固件钩子（PR-N-wifi-1）
 *
 * 探测 TOYOS 卷上是否有 blob；本刀不灌芯片、不常驻大缓冲（wifi-2）。
 */
#include "WifiPrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "ToySerialLog.h"

int WifiFwTryLoad(void) {
    UINT8 Probe[4];
    UINTN Sz = 0;
    int Err;

    gWifiFwOk = 0;
    Err = FileSystemReadFile(WIFI_FW_PATH_A, Probe, sizeof(Probe), &Sz);
    if (Err != FAT_OK || Sz == 0) {
        Sz = 0;
        Err = FileSystemReadFile(WIFI_FW_PATH_B, Probe, sizeof(Probe), &Sz);
    }
    if (Err != FAT_OK || Sz == 0) {
        ToyLogDrv("Boot: rtl8188eu fw miss (FW/RTL8188EU.BIN)\n");
        return 0;
    }
    gWifiFwOk = 1;
    ToyLogDrv("Boot: rtl8188eu fw present\n");
    return 1;
}
