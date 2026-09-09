/*
 * HAL/X64/HalSerial.c — 调试日志门面
 *
 * 输出契约（串口是旁路，不得影响桌面/输入主路径）：
 *   1) 常驻 ring（Desktop 可叠画历史）
 *   2) COM1 TX：Probe 到才 SerialWrite；没有则不碰 UART
 *   3) COM1 RX→Shell 保留（CoolTerm）；Tasks 每轮限量读，勿抽干堵死 USB
 *   4) GOP 镜像：仅 boot/PHOTO；与有无 COM1 无关，进调度前关闭
 *
 * 家侧曾误判「插串口才能打字」：无 COM1 时 Debug 更快狂刷无锁 Present；
 * 有 COM1 时 UART 空等拖慢，竞态变轻——与串口叫醒无关（IER=0）。
 */
#include "HalSerial.h"
#include "HalVideo.h"
#include "HalDevices.h"
#include "Serial.h"
#include "Font.h"

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
        HalVideoPresent();
    }
    HalVideoDrawStringAt(BOOT_LOG_X, gBootLogY, gLine, 0x00FFFFFFu);
    gBootLogY += LineH;
    gLineLen = 0;
    if (!gGopBatch) {
        HalVideoPresent();
    }
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
        RingAppend("boot: no COM1; on-screen log only\n");
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

void HalSerialWrite(const char *Text) {
    if (!Text) {
        return;
    }
    /* 拼行缓冲：跨多次 Write 的 "try BAR=" + hex + "\n" 必须进同一 ring */
    RingAppend(Text);
    /*
     * 有 COM1：只写串口（CoolTerm 可复制），不刷 GOP——真机 BootMark/枚举曾
     * 占满屏幕而串口像「被删」。无 COM1 才镜像到屏。
     */
    if (SerialPresent()) {
        SerialWrite(Text);
        return;
    }
    if (gGopMirror && gVideoUp && !gGopMute && !gPhotoHold) {
        GopWrite(Text);
    }
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
 * 真机 boot 进度：有串口则只 COM1；无串口才直写 scanout 黄字。
 */
void HalSerialBootMark(const char *Text) {
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
        SerialWrite(Text);
        return;
    }
    if (!gVideoUp || gPhotoHold) {
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
    char Msg[120];
    char Diag[80];
    const char *P = "PHOTO ";
    int N = 0;

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
    Msg[N++] = '\n';
    Msg[N] = 0;
    HalSerialWrite(Msg);
}

/* 只盯 k=/m=：d=/r= 每秒都涨，不能当「有输入」 */
static int PhotoParseTag(const char *S, char Tag, UINT32 *Out) {
    int i;

    if (!S || !Out) {
        return 0;
    }
    for (i = 0; S[i]; i++) {
        if (S[i] == Tag && S[i + 1] == '=') {
            UINT32 V = 0;
            i += 2;
            while (S[i] >= '0' && S[i] <= '9') {
                V = V * 10u + (UINT32)(S[i] - '0');
                i++;
            }
            *Out = V;
            return 1;
        }
    }
    return 0;
}

/*
 * 真机 xHCI Arm 后读秒：COM1 打 PHOTO。
 * 只打 begin / k或m变化 / 每5s心跳 / end（勿因 d=Drain 每秒刷屏）。
 */
void HalSerialGopPhotoHold(UINT32 Seconds) {
    UINT32 Left;
    UINT64 T0;
    UINT64 Now;
    UINT64 OneSec;
    char Diag[80];
    UINT32 PrevK = 0;
    UINT32 PrevM = 0;
    UINT32 SincePrint;

    if (Seconds == 0) {
        return;
    }
    gPhotoHold = 1;
    HalSerialGopMute(1);

    Diag[0] = 0;
    HalInputDiagFormat(Diag, (int)sizeof(Diag));
    HalSerialWrite("PHOTO begin");
    HalSerialWrite(Diag);
    HalSerialWrite("\n");
    (void)PhotoParseTag(Diag, 'k', &PrevK);
    (void)PhotoParseTag(Diag, 'm', &PrevM);

    /* 与 StallMs 同口径：~3GHz → 1s */
    OneSec = 3000000000ULL;
    SincePrint = 0;
    for (Left = Seconds; Left > 0; Left--) {
        UINT32 K = 0;
        UINT32 M = 0;
        int Changed;

        T0 = ReadTsc();
        do {
            HalInputPoll();
            __asm__ volatile ("pause");
            Now = ReadTsc();
        } while (Now - T0 < OneSec);

        Diag[0] = 0;
        HalInputDiagFormat(Diag, (int)sizeof(Diag));
        (void)PhotoParseTag(Diag, 'k', &K);
        (void)PhotoParseTag(Diag, 'm', &M);
        Changed = (K != PrevK) || (M != PrevM);
        SincePrint++;
        if (Changed || SincePrint >= 5 || Left == 1) {
            PhotoMarkLeft(Left);
            PrevK = K;
            PrevM = M;
            SincePrint = 0;
        }
    }

    Diag[0] = 0;
    HalInputDiagFormat(Diag, (int)sizeof(Diag));
    HalSerialWrite("PHOTO end");
    HalSerialWrite(Diag);
    HalSerialWrite("\n");

    gPhotoHold = 0;
    HalSerialGopMute(0);
    HalSerialGopMirror(0);
    HalSerialWrite("boot: PHOTO done\n");
}
