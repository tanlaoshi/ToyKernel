/*
 * VideoPriv.h — Video 驱动内部共享头（仅 HAL/X64/Drivers/Video*.c）
 *
 * 禁止 Common / User 包含；对外用 Video.h / HalVideo.h。
 * PR-H-video-split-1：与 VideoBochs.c / VideoScale.c 一并引入。
 */
#ifndef VIDEO_PRIV_H
#define VIDEO_PRIV_H

#include "Video.h"
#include "Font.h"
#include "PhysicalMemory.h"
#include "Hal.h"

/* Bochs/QEMU VBE DISPI（OVMF QemuVideo 同端口） */
#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01D0
#define VBE_DISPI_INDEX_ID      0x0
#define VBE_DISPI_INDEX_XRES    0x1
#define VBE_DISPI_INDEX_YRES    0x2
#define VBE_DISPI_INDEX_BPP     0x3
#define VBE_DISPI_INDEX_ENABLE  0x4
#define VBE_DISPI_INDEX_BANK    0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa
#define VBE_DISPI_ID0           0xB0C0
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40

#define PRESENT_CHUNK_ROWS 64u

/* 全局定义在 Video.c */
extern SCREEN_INFO gScreen;
extern UINT32 gBackground;
extern int gClipOn;
extern UINT32 gClipX;
extern UINT32 gClipY;
extern UINT32 gClipW;
extern UINT32 gClipH;
extern UINT32 gClipBg;

extern UINT32 *gFront;
extern UINT32  gFrontPitch;
extern UINT32 *gBack;
extern UINT32  gBackPitch;
extern UINT32  gBackPages;
extern int     gBackOn;
extern int     gForceFront;

extern UINT32  gPhysW;
extern UINT32  gPhysH;
extern UINT32  gUiScale;

extern int     gDirty;
extern UINT32  gDx0;
extern UINT32  gDy0;
extern UINT32  gDx1;
extern UINT32  gDy1;
extern int     gCurDirty;
extern UINT32  gCx0;
extern UINT32  gCy0;
extern UINT32  gCx1;
extern UINT32  gCy1;

extern int     gCursorOverlay;

/* Scale / 逻辑分辨率（VideoScale.c） */
UINT32 NormalizeUiScale(UINT32 Percent);
void ApplyLogicalFromPhys(void);

/* Dirty / Present（VideoPresent.c；Draw* 经 DirtyUnion 记账） */
void DirtyUnionInto(int *Dirty, UINT32 *Dx0, UINT32 *Dy0, UINT32 *Dx1,
                    UINT32 *Dy1, UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void DirtyUnion(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void DirtyUnionCursor(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);

#endif
