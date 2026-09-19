/*
 * EditUiPrivate.h — EditUi 内部共享头（仅 Common/Services/EditUi 使用）
 *
 * 禁止 User 程序、HAL、Common/Core 包含本文件。
 * 源文件在 Common/Services/EditUi/（核心 EditUi.c）。
 * 对外 API 仍在 EditUi.h。
 */
#ifndef EDIT_UI_PRIVATE_H
#define EDIT_UI_PRIVATE_H

#include "EditUi.h"
#include "Gui.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Font.h"
#include "UI.h"
#include "Hal.h"
#include "Theme.h"

/* ===== 宏（从 EditUi.c 搬入；值不变） ===== */
#define EDIT_PATH_MAX   96
#define EDIT_BUF_MAX    2048
#define EDIT_STATUS_MAX 64

/* ===== 全局（定义在 EditUi.c） ===== */
extern char gPath[EDIT_PATH_MAX];
extern char gBuf[EDIT_BUF_MAX];
extern UINTN gEditLen; /* 与 Console 的 gLen 区分 */
extern UINTN gCursor;
extern int gScrollLine;
extern int gEditDirty; /* 与 Video 的 gDirty 区分 */
extern char gEditStatus[EDIT_STATUS_MAX]; /* 与 FilesUi 的 gStatus 区分 */

extern UINT32 gEditSaveX; /* 与 GuiWm 的 gSaveX 区分 */
extern UINT32 gEditSaveY;
extern UINT32 gSaveButtonWidth;
extern UINT32 gSaveButtonHeight;
extern int gSaveButtonHit;

/* FilesUi 的 CopyStr 已是全局，签名虽同也不能再导出。 */
static inline void CopyStr(char *Dst, int Max, const char *Src) {
    int i = 0;

    if (Max <= 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i < Max - 1) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

/* ===== EditUi.c ===== */
void EditSetStatus(const char *S);
UINTN LineStartOf(UINTN Pos);
UINTN LineIndexOf(UINTN Pos);
void EditClampScroll(UINT32 VisLines);
int InsertChar(char C);
void DeleteAt(UINTN Pos);

#endif
