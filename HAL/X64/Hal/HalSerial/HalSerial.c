/*
 * HAL/X64/Hal/HalSerial/HalSerial.c — 调试日志门面
 *
 * 输出契约（串口是旁路，不得影响桌面/输入主路径）：
 *   1) 常驻 ring（Desktop 可叠画历史）
 *   2) COM1 TX：Probe 到才 SerialWrite；没有则不碰 UART
 *   3) COM1 RX→Shell 保留（CoolTerm）；Tasks 每轮限量读，勿抽干堵死 USB
 *   4) GOP：Mute 禁 Present；BootMark 仍可直写 front；进调度前关镜像
 *   5) 开机屏：单视口上滚（勿整页清屏）；只画 BOOT 关键行（类 Ubuntu）
 *
 * xHCI 枚举期 Mute 挡住普通镜像 Present；BootMark 不受 Mute，避免屏停字。
 */

#include "HalSerial.h"
#include "HalSerialPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Serial.h"
#include "Font.h"
#include "ToySerialConfig.h"
#include "XHCI.h"
#include "Ehci.h"

static char gRing[GOP_RING];
static UINTN gRingLen;
int gVideoUp;
int gGopBanner;
int gGopMute;
static int gSerialReady; /* HalSerialInitialize 已跑过（幂等；模块表可再调） */
/*
 * 1 = boot 期间把日志画到 GOP（与有无 COM1 无关）。
 * 0 = 桌面阶段：只 ring；有 COM1 再旁路写串口。
 */
int gGopMirror = 1;
UINT32 gBootLogY;
char gHalSerialLine[BOOT_LOG_LINE_MAX];
UINTN gLineLen;
/* 可见行缓冲：上滚改软重绘，避免 4K live-front 搬屏波浪/极慢 */
char gBootVis[BOOT_VIS_MAX][BOOT_LOG_LINE_MAX];
UINT32 gBootVisN;

void HalSerialBootFontApply(void) {
    UINT32 W;
    UINT32 H;
    UINT32 Id;

    HalVideoGetSize(&W, &H);
    (void)W;
    if (H >= 2160u) {
        Id = 1u; /* Terminus x2：LineH≈72 → 4K 约 30 行 */
    } else if (H >= 1440u) {
        Id = 0u; /* 16x32：LineH≈36 */
    } else {
        Id = 2u; /* 10x18：与 Theme 默认一致 */
    }
    if (Id >= FontCount()) {
        Id = 0u;
    }
    (void)FontSetById(Id);
}

void RingAppend(const char *Text) {
    while (Text && *Text) {
        if (gRingLen + 1 >= GOP_RING) {
            UINTN Keep = GOP_RING / 2;
            UINTN i;
            for (i = 0; i < Keep; i++) {
                gRing[i] = gRing[gRingLen - Keep + i];
            }
            gRingLen = Keep;
        }
        gRing[gRingLen++] = *Text++;
    }
}

void HalSerialInitialize(void) {
    int KeepGop = gVideoUp;

    /*
     * PR-K-log-uart：Startup / KernelMain 与模块表 serial 均可调用。
     * 第二次起只保证驱动已 Initialize，不再清 ring、不再打横幅。
     */
    if (gSerialReady) {
        SerialInitialize();
        return;
    }

    SerialInitialize();
    gRingLen = 0;
    gLineLen = 0;
    /*
     * KernelMain 可能已 HalSerialGopEnable（接 Boot 黑底上滚）。
     * 若此处无条件 gVideoUp=0，真机屏会永远停在第一条 [Mod] Serial。
     */
    if (!KeepGop) {
        gVideoUp = 0;
        gGopBanner = 0;
        gBootVisN = 0;
        gBootLogY = BootLogBodyY(BootLogLineH());
    }
    if (!SerialPresent()) {
#if TOY_SERIAL
        RingAppend("Boot: No COM1; On-Screen Log Only\n");
#else
        RingAppend("Boot: Serial Disabled (TOY_SERIAL=0)\n");
#endif
    } else {
        /* ASCII only — NUC terminals often mangled UTF-8 */
        SerialWrite("ToyKernel\n");
        SerialWrite("COM1 Serial OK\n");
        RingAppend("ToyKernel\n");
        RingAppend("COM1 Serial OK\n");
    }
    gSerialReady = 1;
}

int HalSerialPresent(void) {
    return SerialPresent();
}

void HalSerialGopEnable(void) {
    /*
     * PR-K-log-cont：已启用则保持上滚位置与横幅，勿二次 GopBannerOnce 抹字。
     * KernelMain 与 video 模块均可调用。
     * PR-K-log-4kfont：每次都套 boot 字号（Video 里 Font/Theme 再 Init 会缩回 10x18）。
     */
    HalSerialBootFontApply();
    if (gVideoUp) {
        return;
    }
    gVideoUp = 1;
    gGopBanner = 0;
    gBootVisN = 0;
    gBootLogY = BootLogBodyY(BootLogLineH());
    gLineLen = 0;
#if TOY_SCREEN_LOG
    /*
     * 勿把整段 ring 一次性刷屏。只起横幅；其后受 SCREEN_LOG_* 上滚。
     */
    GopBannerOnce();
#else
    gGopBanner = 1; /* 跳过黄字 ToyOS boot */
#endif
}

const char *HalSerialLogText(void) {
    if (gRingLen >= GOP_RING) {
        gRingLen = GOP_RING - 1;
    }
    gRing[gRingLen] = '\0';
    return gRing;
}

/* 关镜像后有/无 COM1 行为一致：主路径不再因 Debug→Present 分叉 */
void HalSerialGopMirror(int Enable) {
    gGopMirror = Enable ? 1 : 0;
}

int HalSerialGopMirroring(void) {
    return (gGopMirror && gVideoUp && !gGopMute) ? 1 : 0;
}

/* 真机 xHCI 枚举：Mute 禁 Present/后缓冲 blit，避免与 poll 打架。
 * BootMark 仍直写 front（GopWrite 不 Present），否则屏停在 try# 而串口继续。 */
void HalSerialGopMute(int Mute) {
    gGopMute = Mute ? 1 : 0;
}

int HalSerialDataReady(void) {
    XhciFtdiPollRx();
    if (XhciFtdiDataReady()) {
        return 1;
    }
    EhciFtdiPollRx();
    if (EhciFtdiDataReady()) {
        return 1;
    }
    XhciCdcPollRx();
    if (XhciCdcDataReady()) {
        return 1;
    }
    return SerialDataReady();
}

char HalSerialReadChar(void) {
    if (XhciFtdiDataReady()) {
        return XhciFtdiReadChar();
    }
    if (EhciFtdiDataReady()) {
        return EhciFtdiReadChar();
    }
    if (XhciCdcDataReady()) {
        return XhciCdcReadChar();
    }
    return SerialReadChar();
}

void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits) {
    SerialHexFormat(Buf, Value, Digits);
}

/* 只重置写行 Y，不清屏 Present——RS 后全屏 FillRect 易挂 */
void HalSerialBootLogRewind(void) {
    UINT32 LineH;

    if (!gVideoUp) {
        return;
    }
    GopBannerOnce();
    LineH = BootLogLineH();
    gBootVisN = 0;
    gBootLogY = BootLogBodyY(LineH);
    gLineLen = 0;
}
