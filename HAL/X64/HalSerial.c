/*
 * HAL/X64/HalSerial.c — 串口门面；PR-H3：无 COM1 时镜像到 GOP
 *
 * 真机：即使 Probe 到 COM1，也常驻 gRing 并 Desktop 叠画。
 * Boot 日志必须跨多次 HalSerialWrite 拼行（"try BAR=" + hex + "\n"），
 * 绝不能把半截字符串当成一行，否则会出现左侧竖排 0x.. 叠字。
 */
#include "HalSerial.h"
#include "HalVideo.h"
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
        BootLogClearBody(W, H, LineH);
        HalVideoPresent();
    }
    HalVideoDrawStringAt(BOOT_LOG_X, gBootLogY, gLine, 0x00FFFFFFu);
    gBootLogY += LineH;
    gLineLen = 0;
    HalVideoPresent();
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
    SerialInit();
    gRingLen = 0;
    gVideoUp = 0;
    gGopBanner = 0;
    gBootLogY = BOOT_LOG_TITLE_Y + 24;
    gLineLen = 0;
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
    RingAppend(Text);
    /* 先刷屏：COM1 若阻塞，至少还能在真机上看见进度 */
    if (gVideoUp && !gGopMute) {
        GopWrite(Text);
    }
    if (SerialPresent()) {
        SerialWrite(Text);
    }
}

/* 真机 xHCI RS 后枚举：禁 Present，避免清屏/blit 与控制器打架 */
void HalSerialGopMute(int Mute) {
    gGopMute = Mute ? 1 : 0;
}

/*
 * 真机 boot 进度：直写 scanout 固定行，不走后缓冲 Present。
 * 构建戳 xhci-B8：若屏上仍无此字样，说明跑的不是本内核。
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
    }
    if (!gVideoUp) {
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
    /* 只清一条窄带，避免全宽 FillRect 在 UC 帧缓冲上拖死 */
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
