/*
 * HAL/X64/HalSerial.c — 调试日志门面
 *
 * 输出契约（串口是旁路，不得影响桌面/输入主路径）：
 *   1) 常驻 ring（Desktop 可叠画历史；PHOTO 从此刷屏）
 *   2) COM1 TX：Probe 到才 SerialWrite；没有则不碰 UART
 *   3) COM1 RX→Shell 保留（CoolTerm）；Tasks 每轮限量读，勿抽干堵死 USB
 *   4) GOP：Mute 期间禁止一切帧缓冲写（含 BootMark）；进调度前关镜像
 *   5) 开机屏：单视口上滚（勿整页清屏）；只画 BOOT 关键行（类 Ubuntu）
 *
 * xHCI 枚举期 Mute 必须挡住 BootMark/Present，避免与 poll/DMA 打架。
 */
#include "HalSerial.h"
#include "HalVideo.h"
#include "HalDevices.h"
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
static int gPhotoHold; /* 读秒：禁 Gop 卷屏/禁 BootMark 盖白字 */
static int gSerialReady; /* HalSerialInitialize 已跑过（幂等；模块表可再调） */
/*
 * 1 = boot/PHOTO 期间把日志画到 GOP（与有无 COM1 无关）。
 * 0 = 桌面阶段：只 ring；有 COM1 再旁路写串口。
 */
static int gGopMirror = 1;
static UINT32 gBootLogY;
static char gLine[BOOT_LOG_LINE_MAX];
static UINTN gLineLen;

/* PR-K-log-geom：行高跟字体，勿硬地板 24（否则 4K 竖向像只用上半屏） */
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

/* 屏上 bring-up：BOOT + FS/GUI 进度同文；SMP/MEM/USB 细日志留 ring/串口（USB 里程碑走 BootMark） */
static int ChannelGopOn(int Channel) {
    return Channel == TOY_SLOG_BOOT || Channel == TOY_SLOG_FS ||
           Channel == TOY_SLOG_GUI;
}

/*
 * Ubuntu 式上滚：body 整体上移一行，底行清空；保留顶栏标题。
 * 真机 boot 直写 front，勿 Present。
 */
static void BootLogScrollUp(UINT32 W, UINT32 H, UINT32 LineH) {
    UINT32 BodyY = BootLogBodyY(LineH);
    UINT32 Bottom;
    UINT32 Height;

    if (W == 0) {
        W = 1024;
    }
    if (H <= BodyY + LineH + BOOT_LOG_MARGIN) {
        return;
    }
    Bottom = H - BOOT_LOG_MARGIN;
    if (Bottom <= BodyY + LineH) {
        return;
    }
    Height = Bottom - BodyY - LineH;
    HalVideoDrawBeginFront();
    HalVideoCopyRect(0, BodyY + LineH, 0, BodyY, W, Height);
    HalVideoFillRect(0, Bottom - LineH, W, LineH, 0x00000000u);
    HalVideoDrawEndFront();
    gBootLogY = Bottom - LineH;
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
    gBootLogY = BootLogBodyY(LineH);
    gLineLen = 0;
    gGopBanner = 1;
    /* 横幅一次性 Present 可接受（早于 xHCI）；其后镜像不再 Present */
    HalVideoPresent();
}

static int gGopBatch; /* PhotoHold：凑齐再 Present，且禁止卷屏清掉枚举日志 */

static void GopFlushLine(void) {
    UINT32 W;
    UINT32 H;
    UINT32 LineH;
    UINT32 Limit;

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
    /* 整行字形必须在 Limit 之上；满则上滚一行，勿整页清黑 */
    Limit = (H > LineH + BOOT_LOG_MARGIN) ? (H - LineH - BOOT_LOG_MARGIN) : BootLogBodyY(LineH);
    if (gBootLogY > Limit) {
        if (gGopBatch || gPhotoHold) {
            /* 拍照/读秒：停笔，保留已画白字 */
            gLineLen = 0;
            return;
        }
        BootLogScrollUp(W, H, LineH);
    }
    /*
     * boot 镜像：直写 front，禁止 Present。
     * Present/后缓冲 blit 曾与真机 xHCI poll 互斥 →「拔串口键盘死」。
     */
    HalVideoDrawBeginFront();
    HalVideoDrawStringAt(BOOT_LOG_X, gBootLogY, gLine, 0x00FFFFFFu);
    HalVideoDrawEndFront();
    gBootLogY += LineH;
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
     */
    if (gVideoUp) {
        return;
    }
    gVideoUp = 1;
    gGopBanner = 0;
    gBootLogY = BootLogBodyY(BootLogLineH());
    gLineLen = 0;
    /*
     * 勿把整段 ring（SMP hello / MEM 细节）一次性刷屏——会翻多「页」。
     * 只起横幅；之后 BOOT/FS/GUI 通道与 BootMark 单视口上滚。
     */
    GopBannerOnce();
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
    if (!Text || !gGopMirror || !gVideoUp || gGopMute || gPhotoHold) {
        return;
    }
    GopWrite(Text);
}

void HalSerialWriteChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    /* ring 始终收（PHOTO / Desktop） */
    RingAppend(Text);
    if (SerialPresent() && ChannelUartOn(Channel)) {
        SerialWrite(Text);
    }
    /* 屏：仅 BOOT；有无 COM1 都画（Mute 期除外） */
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

/* 真机 xHCI RS 后枚举：禁 Present，避免清屏/blit 与控制器打架 */
void HalSerialGopMute(int Mute) {
    gGopMute = Mute ? 1 : 0;
}

/*
 * 真机 boot 进度：ring 始终；UART 受通道；屏上并入同路上滚 boot log。
 * USB BootMark（键鼠里程碑）也上屏；细日志应走 ToyLogUsb 不上屏。
 */
void HalSerialBootMarkChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    RingAppend(Text);
    if (SerialPresent() && ChannelUartOn(Channel)) {
        SerialWrite(Text);
    }
    if (Channel == TOY_SLOG_BOOT || Channel == TOY_SLOG_USB) {
        GopMirrorLine(Text);
    }
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
    gBootLogY = BootLogBodyY(LineH);
    gLineLen = 0;
}

static UINT64 ReadTsc(void) {
    UINT32 Lo;
    UINT32 Hi;

    __asm__ volatile ("rdtsc" : "=a"(Lo), "=d"(Hi));
    return ((UINT64)Hi << 32) | Lo;
}

static void PhotoMarkLeft(UINT32 Left) {
    char Msg[192];
    char Diag[144];
    const char *P = "PHOTO ";
    int N = 0;
    UINT32 W;
    UINT32 H;
    UINT32 LineH;
    UINT32 Y;

    while (*P && N < 8) {
        Msg[N++] = *P++;
    }
    Msg[N++] = (char)('0' + ((Left / 10) % 10));
    Msg[N++] = (char)('0' + (Left % 10));
    Msg[N++] = 's';
    Diag[0] = 0;
    HalInputDiagFormat(Diag, (int)sizeof(Diag));
    {
        int i = 0;
        while (Diag[i] && N + 1 < (int)sizeof(Msg) - 1) {
            Msg[N++] = Diag[i++];
        }
    }
    Msg[N] = 0;
    /* 串口旁路一份；屏底直写（不经 Mute） */
    if (SerialPresent()) {
        SerialWrite(Msg);
        SerialWrite("\n");
    }
    if (!gVideoUp) {
        return;
    }
    HalVideoGetSize(&W, &H);
    LineH = BootLogLineH();
    if (H > LineH + 8) {
        Y = H - LineH - 8;
    } else {
        Y = BootLogBodyY(LineH);
    }
    if (W == 0) {
        W = 1024;
    }
    HalVideoDrawBeginFront();
    HalVideoFillRect(0, Y, W, LineH + 2, 0x00000000u);
    HalVideoDrawStringAt(BOOT_LOG_X, Y, Msg, 0x00FFFF00u);
    HalVideoDrawEndFront();
}

/*
 * 真机读秒：接住已上滚的 boot 日志（只改顶栏标题），底栏 PHOTO 读秒。
 * 默认秒数由调用方决定。
 */
void HalSerialGopPhotoHold(UINT32 Seconds) {
    UINT32 W;
    UINT32 H;
    UINT32 LineH;
    UINT32 Left;
    UINT64 T0;
    UINT64 Now;
    UINT64 OneSec;

    if (Seconds == 0) {
        return;
    }
    if (Seconds > 120) {
        Seconds = 120; /* 上限防误传；真机拍照常用 30 */
    }

    /*
     * 必须 Mute：PHOTO 中 Present/后缓冲 blit 会与真机 xHCI poll
     * 打架 → k= 一直为 0。无串口时改为直写 front（BeginFront），不 Present。
     */
    gPhotoHold = 1;
    HalSerialGopMute(1);

    if (gVideoUp) {
        /*
         * PR-K-log-cont：勿全屏清黑再刷 ring——NUC 上会从「ToyOS boot」跳成「PHOTO」闪屏。
         * 屏上已有连续上滚日志；只改顶栏标题，底栏读秒由 PhotoMarkLeft 更新。
         */
        HalVideoGetSize(&W, &H);
        LineH = BootLogLineH();
        if (W == 0) {
            W = 1024;
        }
        (void)H;
        HalVideoDrawBeginFront();
        HalVideoFillRect(0, 0, W, BootLogBodyY(LineH), 0x00000000u);
        HalVideoDrawStringAt(BOOT_LOG_X, BOOT_LOG_TITLE_Y, "ToyOS PHOTO",
                             0x00FFFF00u);
        HalVideoDrawEndFront();
    }

    OneSec = 3000000000ULL;
    for (Left = Seconds; Left > 0; Left--) {
        PhotoMarkLeft(Left);
        T0 = ReadTsc();
        do {
            HalInputPoll();
            if (HalPowerButtonPressed()) {
                HalSerialBootMark("Boot: Power Button -> Shutdown\n");
                HalCpuShutdown();
            }
            __asm__ volatile ("pause");
            Now = ReadTsc();
        } while (Now - T0 < OneSec);
    }

    gPhotoHold = 0;
    HalSerialGopMute(0);
    HalSerialGopMirror(0);
    HalSerialBootMark("Boot: PHOTO Done\n");
}
