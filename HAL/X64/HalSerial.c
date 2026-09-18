/*
 * HAL/X64/HalSerial.c — 调试日志门面
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
#include "HalVideo.h"
#include "Hal.h"
#include "Serial.h"
#include "Font.h"
#include "ToySerialConfig.h"

#define GOP_RING 4096
#define BOOT_LOG_X 8u
#define BOOT_LOG_TITLE_Y 8u
#define BOOT_LOG_MARGIN 8u
/* PR-K-log-geom：4K/8px ≈ 480 列；512 够满宽一行，超宽软换行 */
#define BOOT_LOG_LINE_MAX 512u

static char gRing[GOP_RING];
static UINTN gRingLen;
static int gVideoUp;
static int gGopBanner;
static int gGopMute;
static int gSerialReady; /* HalSerialInitialize 已跑过（幂等；模块表可再调） */
/*
 * 1 = boot 期间把日志画到 GOP（与有无 COM1 无关）。
 * 0 = 桌面阶段：只 ring；有 COM1 再旁路写串口。
 */
static int gGopMirror = 1;
static UINT32 gBootLogY;
static char gLine[BOOT_LOG_LINE_MAX];
static UINTN gLineLen;
/* 可见行缓冲：上滚改软重绘，避免 4K live-front 搬屏波浪/极慢 */
#define BOOT_VIS_MAX 64u
static char gBootVis[BOOT_VIS_MAX][BOOT_LOG_LINE_MAX];
static UINT32 gBootVisN;

/*
 * PR-K-log-geom：行高跟字体，勿硬地板 24。
 * PR-K-log-4kfont：高分屏若仍用 Theme 默认 10x18，4K 约百余行，
 * 开机日志写不满 → 上滚测不到；按 H 选大字，镜像期冻结。
 * 内建表序（FontInitialize）：0=16x32，1=x2，2=10x18。
 */
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

static UINT32 BootLogLineH(void) {
    UINT32 LineH = FontAdvanceY();
    if (LineH == 0) {
        LineH = 16;
    }
    return LineH;
}

static UINT32 BootLogCellW(void) {
    UINT32 Cell = FontAdvanceX();
    if (Cell == 0) {
        Cell = 8;
    }
    return Cell;
}

/* 当前分辨率下屏上最多几列（不含边距） */
static UINT32 BootLogMaxCols(void) {
    UINT32 W;
    UINT32 H;
    UINT32 Cell;
    UINT32 Cols;

    HalVideoGetSize(&W, &H);
    (void)H;
    if (W == 0) {
        W = 1024;
    }
    if (W <= BOOT_LOG_X + BOOT_LOG_MARGIN) {
        return 1;
    }
    Cell = BootLogCellW();
    Cols = (W - BOOT_LOG_X - BOOT_LOG_MARGIN) / Cell;
    if (Cols == 0) {
        Cols = 1;
    }
    if (Cols > BOOT_LOG_LINE_MAX - 1) {
        Cols = BOOT_LOG_LINE_MAX - 1;
    }
    return Cols;
}

static UINT32 BootLogBodyY(UINT32 LineH) {
    return BOOT_LOG_TITLE_Y + LineH + 8;
}

/* PR-K-log-switch：屏上通道跟 TOY_SCREEN_LOG_*；运行时仍受 Mirror/Mute */
static int ChannelGopOn(int Channel) {
#if !TOY_SCREEN_LOG
    (void)Channel;
    return 0;
#else
    switch (Channel) {
    case TOY_SLOG_BOOT:
        return TOY_SCREEN_LOG_BOOT;
    case TOY_SLOG_USB:
        return TOY_SCREEN_LOG_USB;
    case TOY_SLOG_SMP:
        return TOY_SCREEN_LOG_SMP;
    case TOY_SLOG_GUI:
        return TOY_SCREEN_LOG_GUI;
    case TOY_SLOG_NET:
        return TOY_SCREEN_LOG_NET;
    case TOY_SLOG_FS:
        return TOY_SCREEN_LOG_FS;
    case TOY_SLOG_MEM:
        return TOY_SCREEN_LOG_MEM;
    case TOY_SLOG_DRV:
        return TOY_SCREEN_LOG_DRV;
    case TOY_SLOG_MISC:
    default:
        return TOY_SCREEN_LOG_MISC;
    }
#endif
}

/*
 * Ubuntu 式上滚：可见行在 RAM，满则丢最旧行并整区重绘。
 * 勿 CopyRect/memmove live front——4K 既波浪又极慢。
 */
static UINT32 BootLogVisCap(UINT32 H, UINT32 LineH) {
    UINT32 BodyY = BootLogBodyY(LineH);
    UINT32 Bottom;
    UINT32 Rows;

    if (H <= BodyY + LineH + BOOT_LOG_MARGIN) {
        return 1;
    }
    Bottom = H - BOOT_LOG_MARGIN;
    Rows = (Bottom - BodyY) / LineH;
    if (Rows == 0) {
        Rows = 1;
    }
    if (Rows > BOOT_VIS_MAX) {
        Rows = BOOT_VIS_MAX;
    }
    return Rows;
}

static void BootLogCopyRow(char *Dst, const char *Src) {
    UINTN i = 0;

    if (!Dst) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i + 1 < BOOT_LOG_LINE_MAX) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

static void BootLogRepaintBody(UINT32 W, UINT32 H, UINT32 LineH) {
    UINT32 BodyY = BootLogBodyY(LineH);
    UINT32 Bottom;
    UINT32 i;
    UINT32 Y;

    if (W == 0) {
        W = 1024;
    }
    Bottom = (H > BOOT_LOG_MARGIN) ? (H - BOOT_LOG_MARGIN) : BodyY;
    HalVideoDrawBeginFront();
    if (Bottom > BodyY) {
        HalVideoFillRect(0, BodyY, W, Bottom - BodyY, 0x00000000u);
    }
    Y = BodyY;
    for (i = 0; i < gBootVisN; i++) {
        HalVideoDrawStringAt(BOOT_LOG_X, Y, gBootVis[i], 0x00FFFFFFu);
        Y += LineH;
    }
    HalVideoDrawEndFront();
    gBootLogY = Y;
}

static void BootLogPushVisible(UINT32 W, UINT32 H, UINT32 LineH) {
    UINT32 Cap = BootLogVisCap(H, LineH);
    UINT32 i;

    if (gBootVisN >= Cap) {
        for (i = 1; i < gBootVisN; i++) {
            BootLogCopyRow(gBootVis[i - 1], gBootVis[i]);
        }
        if (gBootVisN > 0) {
            gBootVisN--;
        }
        BootLogCopyRow(gBootVis[gBootVisN], gLine);
        gBootVisN++;
        BootLogRepaintBody(W, H, LineH);
        return;
    }
    BootLogCopyRow(gBootVis[gBootVisN], gLine);
    gBootVisN++;
    HalVideoDrawBeginFront();
    HalVideoDrawStringAt(BOOT_LOG_X, gBootLogY, gLine, 0x00FFFFFFu);
    HalVideoDrawEndFront();
    gBootLogY += LineH;
}

static void RingAppend(const char *Text) {
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

static void GopBannerOnce(void) {
#if !TOY_SCREEN_LOG
    /* SCREEN_LOG=0：不画黄字横幅，也不 Present */
    gGopBanner = 1;
    return;
#else
    {
        UINT32 W;
        UINT32 H;
        UINT32 LineH;

        if (gGopBanner) {
            return;
        }
        HalVideoGetSize(&W, &H);
        if (W == 0) {
            W = 1024;
        }
        LineH = BootLogLineH();
        HalVideoClearClip();
        HalVideoFillRect(0, 0, W, BootLogBodyY(LineH), 0x00000000u);
        HalVideoDrawStringAt(BOOT_LOG_X, BOOT_LOG_TITLE_Y, "ToyOS boot", 0x00FFFF00u);
        gBootVisN = 0;
        gBootLogY = BootLogBodyY(LineH);
        gLineLen = 0;
        gGopBanner = 1;
        /* 横幅一次性 Present 可接受（早于 xHCI）；其后镜像不再 Present */
        HalVideoPresent();
    }
#endif
}

static int gGopBatch; /* PhotoHold：凑齐再 Present，且禁止卷屏清掉枚举日志 */

static void GopFlushLine(void) {
    UINT32 W;
    UINT32 H;
    UINT32 LineH;
    UINT32 Cap;

    gLine[gLineLen] = '\0';
    if (gLineLen == 0) {
        return;
    }
    GopBannerOnce();
    LineH = BootLogLineH();
    HalVideoGetSize(&W, &H);
    if (H == 0) {
        H = 768;
    }
    Cap = BootLogVisCap(H, LineH);
    if (gBootVisN >= Cap && gGopBatch) {
        /* 批量 Present：停笔，保留已画白字 */
        gLineLen = 0;
        return;
    }
    /*
     * boot 镜像：直写 front，禁止 Present。
     * 满行则软缓冲丢最旧并重绘；未满只追加一行。
     */
    BootLogPushVisible(W, H, LineH);
    gLineLen = 0;
}

static void GopWrite(const char *Text) {
    UINT32 MaxCols;

    if (!Text || !*Text) {
        return;
    }
    GopBannerOnce();
    MaxCols = BootLogMaxCols();
    while (*Text) {
        if (*Text == '\n') {
            GopFlushLine();
            Text++;
            continue;
        }
        /* 满宽软换行：4K 用满横向，不再 160 截断 */
        if (gLineLen >= MaxCols) {
            GopFlushLine();
            MaxCols = BootLogMaxCols();
        }
        if (gLineLen + 1 < sizeof(gLine)) {
            gLine[gLineLen++] = *Text;
        }
        Text++;
    }
    /* 半截行留在 gLine，等下次 '\n' —— 修复 try BAR=/0x../竖排叠字 */
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

static int ChannelUartOn(int Channel) {
#if !TOY_SERIAL
    (void)Channel;
    return 0;
#else
    switch (Channel) {
    case TOY_SLOG_BOOT:
        return TOY_SERIAL_BOOT;
    case TOY_SLOG_USB:
        return TOY_SERIAL_USB;
    case TOY_SLOG_SMP:
        return TOY_SERIAL_SMP;
    case TOY_SLOG_GUI:
        return TOY_SERIAL_GUI;
    case TOY_SLOG_NET:
        return TOY_SERIAL_NET;
    case TOY_SLOG_FS:
        return TOY_SERIAL_FS;
    case TOY_SLOG_MEM:
        return TOY_SERIAL_MEM;
    case TOY_SLOG_DRV:
        return TOY_SERIAL_DRV;
    case TOY_SLOG_MISC:
    default:
        return TOY_SERIAL_MISC;
    }
#endif
}

static void GopMirrorLine(const char *Text) {
    if (!Text || !gGopMirror || !gVideoUp || gGopMute) {
        return;
    }
    GopWrite(Text);
}

void HalSerialWriteChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    /* ring 始终收（boot / Desktop） */
    RingAppend(Text);
    if (SerialPresent() && ChannelUartOn(Channel)) {
        SerialWrite(Text);
    }
    /* 屏：受 TOY_SCREEN_LOG_* + Mirror/Mute（有无 COM1 都可画） */
    if (ChannelGopOn(Channel)) {
        GopMirrorLine(Text);
    }
}

void HalSerialWrite(const char *Text) {
    HalSerialWriteChannel(TOY_SLOG_MISC, Text);
}

void HalSerialWriteChannelHex32(int Channel, UINT32 Value) {
    char Buf[12];

    HalSerialFormatHex(Buf, Value, 8);
    HalSerialWriteChannel(Channel, Buf);
}

void HalSerialWriteChannelHex64(int Channel, UINT64 Value) {
    char Buf[20];

    HalSerialFormatHex(Buf, Value, 16);
    HalSerialWriteChannel(Channel, Buf);
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

/*
 * 真机 boot 进度：ring 始终；UART 受 TOY_SERIAL_*；屏受 TOY_SCREEN_LOG_*。
 * USB BootMark：Mute 期间也上屏（front only）。
 */
void HalSerialBootMarkChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    RingAppend(Text);
    if (SerialPresent() && ChannelUartOn(Channel)) {
        SerialWrite(Text);
    }
    if (!ChannelGopOn(Channel) || !gGopMirror || !gVideoUp) {
        return;
    }
    /* 绕过 gGopMute：里程碑必须看得见；细日志走 ToyLog* → GopMirrorLine 仍受 Mute */
    GopWrite(Text);
}

void HalSerialBootMark(const char *Text) {
    HalSerialBootMarkChannel(TOY_SLOG_BOOT, Text);
}

int HalSerialDataReady(void) {
    return SerialDataReady();
}

char HalSerialReadChar(void) {
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
