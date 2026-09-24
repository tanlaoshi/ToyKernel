/*
 * DevicesUiPrivate.h — DevicesUi 内部（仅 Common/Services/DevicesUi）
 */
#ifndef DEVICES_UI_PRIVATE_H
#define DEVICES_UI_PRIVATE_H

#include "DevicesUi.h"
#include "Device.h"
#include "Driver.h"
#include "Gui.h"
#include "GuiPrivate.h"
#include "HalVideo.h"
#include "Font.h"
#include "Locale.h"
#include "Theme.h"
#include "UI.h"

#define DEVUI_ROW_H     22u
#define DEVUI_PAD       8u
#define DEVUI_SIDE_W    96u
#define DEVUI_MAP_MAX   256
#define DEVUI_FILT_ALL  0
#define DEVUI_FILT_BOUND 1
#define DEVUI_FILT_FREE 2
#define DEVUI_FILT_N    3

extern int gDevUiSel;
extern int gDevUiScroll;
extern int gDevUiCount;
extern int gDevUiFilt;
extern int gDevUiMap[DEVUI_MAP_MAX];
extern int gDevUiFiltCount;
extern UINT32 gDevUiListX;
extern UINT32 gDevUiListY;
extern UINT32 gDevUiListW;
extern UINT32 gDevUiListH;
extern int gDevUiVisible;
extern UINT32 gDevUiSideX;
extern UINT32 gDevUiSideW;
extern UINT32 gDevUiSideRow0;
extern UINT32 gDevUiSideLineH;
extern UINT32 gDevUiPrevX;
extern UINT32 gDevUiPrevW;

void DevicesUiReload(void);
void DevicesUiRebuildFilt(void);
void DevicesUiFormatPci(const DEVICE_NODE *Dev, char *Out, UINTN Max);
void DevicesUiFormatIds(const DEVICE_NODE *Dev, char *Out, UINTN Max);
void DevicesUiPaint(void);

/*
 * PR-DEV-ui-summary-data：系统摘要只读数据模型。
 * 填充走 Hal* / PhysicalMemory* / Block* / BootInfo / Device 公共 API；
 * UI 不读 HAL 私有头。第 2 刀由 Paint 据本结构绘制 About 页。
 */
typedef struct {
    const char *OsVersion;   /* TOY_OS_VERSION_STRING */
    const char *Arch;         /* HalArchName()；可为 NULL */
    const char *CpuInfo;      /* HalCpuInfo() */
    int         CpuCount;     /* HalCpuCount() */
    int         Hypervisor;   /* HalCpuIsHypervisor() */
    UINT64      MemTotalMiB;  /* PhysicalMemoryTotalPages() / 256 */
    UINT64      MemFreeMiB;   /* PhysicalMemoryFreePageCount() / 256 */
    char        Disk[64];      /* 「N ready」或「-」 */
    char        Display[32];   /* 「WxH」或「-」 */
    int         PciCount;     /* DeviceCount() */
} DEVICES_UI_SUMMARY;

void DevicesUiFillSummary(DEVICES_UI_SUMMARY *Out);
/* DEBUG 自检：填结构体并串口打一行；release 编译为空调用 */
void DevicesUiSummarySelfCheck(void);

#endif
