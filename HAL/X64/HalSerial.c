/*
 * HAL/X64/HalSerial.c — 调试日志门面
 *
 * 输出契约（串口是旁路，不得影响桌面/输入主路径）：
 *   1) 常驻 ring（Desktop 可叠画历史；PHOTO 从此刷屏）
 *   2) COM1 TX：Probe 到才 SerialWrite；没有则不碰 UART
 *   3) COM1 RX→Shell 保留（CoolTerm）；Tasks 每轮限量读，勿抽干堵死 USB
 *   4) GOP：Mute 期间禁止一切帧缓冲写（含 BootMark）；进调度前关镜像
 *
 * 「插串口才能打字」真因（非 UART 叫醒）：
 *   有 COM1 → BootMark/Write 只走串口，xHCI 枚举期零帧缓冲 I/O；
 *   无 COM1 → 旧逻辑仍 BootMark 直写 front / Write→Present，与 poll/DMA 打架
 *   → PHOTO k=0。Mute 必须同时挡住 BootMark。
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
#define BOOT_LOG_LINE_MIN 24u

static char gRing[GOP_RING];
static UINTN gRingLen;
static int gVideoUp;
static int gGopBanner;
static int gGopMute;
static int gPhotoHold; /* 读秒：禁 Gop 卷屏/禁 BootMark 盖白字 */
/*
 * 1 = boot/PHOTO 期间把日志画到 GOP（与有无 COM1 无关）。
 * 0 = 桌面阶段：只 ring；有 COM1 再旁路写串口。
 */
static int gGopMirror = 1;
static UINT32 gBootLogY;
static char gLine[160];
static UINTN gLineLen;

static UINT32 BootLogLineH(void) {
    UINT32 LineH = FontAdvanceY();
    /* 真机底行半截：AdvanceY 偏小，固定至少 24px 行距 */
    if (LineH < BOOT_LOG_LINE_MIN) {
        LineH = BOOT_LOG_LINE_MIN;
    }
    return LineH;
}

static UINT32 BootLogBodyY(UINT32 LineH) {
    return BOOT_LOG_TITLE_Y + LineH + 8;
}

static void BootLogClearBody(UINT32 W, UINT32 H, UINT32 LineH) {
    UINT32 BodyY = BootLogBodyY(LineH);
    if (W == 0) {
        W = 1024;
    }
    if (H > BodyY) {
        HalVideoFillRect(0, BodyY, W, H - BodyY, 0x00000000u);
    }
    gBootLogY = BodyY;
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
    HalVideoDrawStringAt(BOOT_LOG_X, BOOT_LOG_TITLE_Y,
                         "ToyOS on-screen boot log", 0x00FFFF00u);
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
    /* 整行字形必须在 Limit 之上；宁可提前卷屏也不出半截字 */
    Limit = (H > LineH + BOOT_LOG_MARGIN) ? (H - LineH - BOOT_LOG_MARGIN) : BootLogBodyY(LineH);
    if (gBootLogY > Limit) {
        if (gGopBatch || gPhotoHold) {
            /* 拍照/读秒：停笔，保留已画白字（勿 ClearBody） */
            gLineLen = 0;
            return;
        }
        BootLogClearBody(W, H, LineH);
        /* 勿 Present：无 COM 镜像路径上 Present 与真机 xHCI 打架 */
    }
    /*
     * 无 COM1 的 boot 镜像：直写 front，禁止 Present。
     * Present/后缓冲 blit 曾与真机 xHCI poll 互斥 →「拔串口键盘死」。
     */
    HalVideoDrawBeginFront();
    HalVideoDrawStringAt(BOOT_LOG_X, gBootLogY, gLine, 0x00FFFFFFu);
    HalVideoDrawEndFront();
    gBootLogY += LineH;
    gLineLen = 0;
}

static void GopWrite(const char *Text) {
    if (!Text || !*Text) {
        return;
    }
    GopBannerOnce();
    while (*Text) {
        if (*Text == '\n') {
            GopFlushLine();
            Text++;
            continue;
        }
        if (gLineLen + 1 < sizeof(gLine)) {
            gLine[gLineLen++] = *Text;
        }
        Text++;
    }
    /* 半截行留在 gLine，等下次 '\n' —— 修复 try BAR=/0x../竖排叠字 */
}

void HalSerialInit(void) {
    int KeepGop = gVideoUp;

    SerialInit();
    gRingLen = 0;
    gLineLen = 0;
    /*
     * KernelMain 可能已 HalSerialGopEnable（H0 清屏后要看 [mod]）。
     * 若此处无条件 gVideoUp=0，真机屏会永远停在第一条 [mod] serial。
     */
    if (!KeepGop) {
        gVideoUp = 0;
        gGopBanner = 0;
        gBootLogY = BOOT_LOG_TITLE_Y + 24;
    }
    if (!SerialPresent()) {
#if TOY_SERIAL
        RingAppend("boot: no COM1; on-screen log only\n");
#else
        RingAppend("boot: serial disabled (TOY_SERIAL=0)\n");
#endif
    } else {
        SerialWrite("boot: COM1 serial ok\n");
        RingAppend("boot: COM1 ok (also on-screen log)\n");
    }
}

int HalSerialPresent(void) {
    return SerialPresent();
}

void HalSerialGopEnable(void) {
    gVideoUp = 1;
    gGopBanner = 0;
    gBootLogY = BOOT_LOG_TITLE_Y + 24;
    gLineLen = 0;
    if (gRingLen > 0) {
        gRing[gRingLen] = '\0';
        GopWrite(gRing);
    } else {
        GopBannerOnce();
    }
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

void HalSerialWriteChannel(int Channel, const char *Text) {
    if (!Text) {
        return;
    }
    /* ring 始终收（PHOTO / Desktop）；UART / 无 COM 镜像受通道约束 */
    RingAppend(Text);
    if (SerialPresent()) {
        if (ChannelUartOn(Channel)) {
            SerialWrite(Text);
        }
        return;
    }
    if (ChannelUartOn(Channel) && gGopMirror && gVideoUp && !gGopMute &&
        !gPhotoHold) {
        GopWrite(Text);
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

/* 真机 xHCI RS 后枚举：禁 Present，避免清屏/blit 与控制器打架 */
void HalSerialGopMute(int Mute) {
    gGopMute = Mute ? 1 : 0;
}

/*
 * 真机 boot 进度：ring 始终；UART 受通道；无 COM 且未 Mute 才直写 front
 *（屏上黄字不因模块 quiet 而消失，便于 PHOTO）。
 */
void HalSerialBootMarkChannel(int Channel, const char *Text) {
    UINT32 W;
    UINT32 H;
    UINT32 LineH;
    UINT32 Y;
    char Line[160];
    UINTN N;

    if (!Text) {
        return;
    }
    RingAppend(Text);
    if (SerialPresent()) {
        if (ChannelUartOn(Channel)) {
            SerialWrite(Text);
        }
        return;
    }
    /* Mute/PHOTO：与「有串口」同形——不上屏，等 PHOTO 刷 ring */
    if (!gVideoUp || gPhotoHold || gGopMute) {
        return;
    }
    N = 0;
    while (Text[N] && Text[N] != '\n' && N + 1 < sizeof(Line)) {
        Line[N] = Text[N];
        N++;
    }
    Line[N] = '\0';
    LineH = BootLogLineH();
    Y = BootLogBodyY(LineH);
    HalVideoGetSize(&W, &H);
    if (W == 0) {
        W = 1024;
    }
    if (W > 960) {
        W = 960;
    }
    HalVideoDrawBeginFront();
    HalVideoFillRect(0, Y, W, LineH + 2, 0x00000000u);
    HalVideoDrawStringAt(BOOT_LOG_X, Y, Line, 0x00FFFF00u);
    HalVideoDrawEndFront();
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
    char Msg[160];
    char Diag[120];
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
 * 真机读秒：刷 ring 尾部到屏（无串口也能拍），底栏 PHOTO；默认秒数由调用方决定。
 */
void HalSerialGopPhotoHold(UINT32 Seconds) {
    UINT32 W;
    UINT32 H;
    UINT32 LineH;
    UINT32 Left;
    UINT32 MaxLines;
    UINT32 LineCount;
    UINT32 Skip;
    UINT32 i;
    UINT64 T0;
    UINT64 Now;
    UINT64 OneSec;
    const char *Log;
    const char *Start;

    if (Seconds == 0) {
        return;
    }
    if (Seconds > 120) {
        Seconds = 120; /* 上限防误传；真机拍照常用 30 */
    }

    /*
     * 必须 Mute：44b1633 在 PHOTO 中 Present/后缓冲 blit 会与真机 xHCI poll
     * 打架 → k= 一直为 0。无串口时改为直写 front（BeginFront），不 Present。
     */
    gPhotoHold = 1;
    HalSerialGopMute(1);

    if (gVideoUp) {
        UINT32 Y;
        const char *P;
        char LineBuf[160];
        int N;

        HalVideoGetSize(&W, &H);
        LineH = BootLogLineH();
        if (H == 0) {
            H = 768;
        }
        if (W == 0) {
            W = 1024;
        }
        MaxLines = 20;
        if (LineH > 0 && H > BootLogBodyY(LineH) + LineH * 3) {
            MaxLines = (H - BootLogBodyY(LineH) - LineH * 3) / LineH;
            if (MaxLines < 8) {
                MaxLines = 8;
            }
            if (MaxLines > 40) {
                MaxLines = 40;
            }
        }

        Log = HalSerialLogText();
        Start = Log ? Log : "";
        LineCount = 0;
        for (i = 0; Start[i]; i++) {
            if (Start[i] == '\n') {
                LineCount++;
            }
        }
        Skip = 0;
        if (LineCount > MaxLines) {
            Skip = LineCount - MaxLines;
            LineCount = 0;
            for (i = 0; Start[i]; i++) {
                if (Start[i] == '\n') {
                    LineCount++;
                    if (LineCount == Skip) {
                        Start = Start + i + 1;
                        break;
                    }
                }
            }
        }

        HalVideoDrawBeginFront();
        HalVideoFillRect(0, 0, W, H, 0x00000000u);
        HalVideoDrawStringAt(BOOT_LOG_X, BOOT_LOG_TITLE_Y,
                             "ToyOS PHOTO (press keys / move mouse)", 0x00FFFF00u);
        Y = BootLogBodyY(LineH);
        if (Skip > 0) {
            HalVideoDrawStringAt(BOOT_LOG_X, Y, "(boot log tail)", 0x00AAAAAAu);
            Y += LineH;
        }
        P = Start;
        while (P && *P && Y + LineH < H - LineH * 3) {
            N = 0;
            while (*P && *P != '\n' && N + 1 < (int)sizeof(LineBuf)) {
                LineBuf[N++] = *P++;
            }
            LineBuf[N] = 0;
            if (N > 0) {
                HalVideoDrawStringAt(BOOT_LOG_X, Y, LineBuf, 0x00FFFFFFu);
            }
            Y += LineH;
            if (*P == '\n') {
                P++;
            }
        }
        {
            char Res[48];
            UINT32 Rw = 0;
            UINT32 Rh = 0;
            int n = 0;
            const char *R = "boot: video ";
            HalVideoGetSize(&Rw, &Rh);
            while (*R && n < 16) {
                Res[n++] = *R++;
            }
            Res[n++] = (char)('0' + ((Rw / 1000) % 10));
            Res[n++] = (char)('0' + ((Rw / 100) % 10));
            Res[n++] = (char)('0' + ((Rw / 10) % 10));
            Res[n++] = (char)('0' + (Rw % 10));
            Res[n++] = 'x';
            Res[n++] = (char)('0' + ((Rh / 1000) % 10));
            Res[n++] = (char)('0' + ((Rh / 100) % 10));
            Res[n++] = (char)('0' + ((Rh / 10) % 10));
            Res[n++] = (char)('0' + (Rh % 10));
            Res[n] = 0;
            HalVideoDrawStringAt(BOOT_LOG_X, Y, Res, 0x00FFFF00u);
            Y += LineH;
            /* PR-G-fb-pte：与 video 同行区直绘，不依赖 ring 尾 */
            {
                char FbLine[96];
                if (HalVideoFbPteLine(FbLine, sizeof(FbLine)) > 0 &&
                    Y + LineH < H - LineH) {
                    HalVideoDrawStringAt(BOOT_LOG_X, Y, FbLine, 0x00FFFF00u);
                }
            }
        }
        HalVideoDrawEndFront();
    }

    OneSec = 3000000000ULL;
    for (Left = Seconds; Left > 0; Left--) {
        PhotoMarkLeft(Left);
        T0 = ReadTsc();
        do {
            HalInputPoll();
            if (HalPowerButtonPressed()) {
                HalSerialBootMark("boot: power button -> shutdown\n");
                HalCpuShutdown();
            }
            __asm__ volatile ("pause");
            Now = ReadTsc();
        } while (Now - T0 < OneSec);
    }

    gPhotoHold = 0;
    HalSerialGopMute(0);
    HalSerialGopMirror(0);
    HalSerialBootMark("boot: PHOTO done\n");
}
