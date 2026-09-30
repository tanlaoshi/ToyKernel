/*
 * ThemeSave.c — 写 THEME.CFG 与 TOYOS.DB（编排；PR-F-theme-1）
 * CFG：ThemeSaveCfg.c；DB：ThemeSaveDb.c；填值：ThemeSaveFmt.c
 */
#include "Theme.h"
#include "ThemePrivate.h"
#include "HalConsole.h"

int ThemeSave(void) {
    char Buf[320];
    UINTN N;
    THEME_SAVE_VALS Vals;
    int CfgSt;
    static int sBusy;

    if (sBusy) {
        HalConsoleWriteSerial("Theme: save reenter skipped\n");
        return -1;
    }
    sBusy = 1;

    /*
     * 先写 THEME.CFG：QEMU edid / ToyBoot 认 CFG；若先写 DB 再 CFG 失败，
     * Settings 显示新分辨率、下次启动仍用旧 edid。
     */
    ThemeSaveFillVals(&Vals);
    N = ThemeSaveBuildCfg(Buf, sizeof(Buf), &Vals);
    CfgSt = ThemeSaveWriteCfg(Buf, N);
    if (CfgSt < 0) {
        sBusy = 0;
        return -1;
    }
    if (CfgSt > 0) {
        /* 内容未变已跳过；不重刷 DB */
        sBusy = 0;
        return 0;
    }
    /*
     * 故意不在 Write 后立刻 Read 同文件：QEMU fat:rw/vvfat 会断言
     * get_cluster_count_for_direntry。Guest 以内存 + DB 为准。
     */
    (void)ThemeSaveWriteDb(&Vals);
    sBusy = 0;
    return 0;
}
