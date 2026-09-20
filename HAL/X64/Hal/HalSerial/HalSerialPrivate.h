/*
 * HalSerialPrivate.h — 开机日志与串口门面（PR-S-halserial-1）
 */
#ifndef HAL_SERIAL_PRIVATE_H
#define HAL_SERIAL_PRIVATE_H

#include "HalSerial.h"
#include "Font.h"

#define GOP_RING 4096
#define BOOT_LOG_X 8u
#define BOOT_LOG_TITLE_Y 8u
#define BOOT_LOG_MARGIN 8u
/* PR-K-log-geom：4K/8px ≈ 480 列；512 够满宽一行，超宽软换行 */
#define BOOT_LOG_LINE_MAX 512u
#define BOOT_VIS_MAX 64u

extern int gVideoUp;
extern int gGopBanner;
extern int gGopMute;
extern int gGopMirror;
extern UINT32 gBootLogY;
extern char gHalSerialLine[BOOT_LOG_LINE_MAX];
extern UINTN gLineLen;
extern char gBootVis[BOOT_VIS_MAX][BOOT_LOG_LINE_MAX];
extern UINT32 gBootVisN;

static inline UINT32 BootLogLineH(void) {
    UINT32 LineH = FontAdvanceY();
    if (LineH == 0) {
        LineH = 16;
    }
    return LineH;
}

static inline UINT32 BootLogBodyY(UINT32 LineH) {
    return BOOT_LOG_TITLE_Y + LineH + 8;
}

void RingAppend(const char *Text);
void GopBannerOnce(void);
void GopWrite(const char *Text);

#endif
