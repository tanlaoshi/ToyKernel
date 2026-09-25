/*
 * HalSerialWrite.c — 通道过滤与写出（PR-S-halserial-1）
 */
#include "HalSerial.h"
#include "HalSerialPrivate.h"
#include "HalVideo.h"
#include "Hal.h"
#include "Serial.h"
#include "Font.h"
#include "ToySerialConfig.h"
#include "SpinLock.h"
#include "XHCI.h"
#include "Ehci.h"

/* PR-TEST：串口输出锁。fork 后父子并发 printf 会字节交错（如 PIPEDEMO 的
 * "root=0x000PING000..."），用自旋锁把每次 HalSerialWriteChannel 调用做成
 * 原子（关中断，避免持锁被定时器打断再抢同锁死锁）。RingAppend/GopWrite
 * 不会回调本函数，无重入风险。零初始化即解锁态。 */
static SPIN_LOCK gSerialLock;

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
    SpinLockAcquire(&gSerialLock);
    /* ring 始终收（boot / Desktop） */
    RingAppend(Text);
    if (SerialPresent() && ChannelUartOn(Channel)) {
        SerialWrite(Text);
    }
    /* 屏：受 TOY_SCREEN_LOG_* + Mirror/Mute（有无 COM1 都可画） */
    if (ChannelGopOn(Channel)) {
        GopMirrorLine(Text);
    }
    SpinLockRelease(&gSerialLock);
    /* PR-H-usb-uart：有 FT232/CDC 则 tee（锁外，避免 Bulk 等事件重入） */
    XhciFtdiWrite(Text);
    EhciFtdiWrite(Text);
    XhciCdcWrite(Text);
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
    if (ChannelGopOn(Channel) && gGopMirror && gVideoUp) {
        /* 绕过 gGopMute：里程碑必须看得见；细日志走 ToyLog* → GopMirrorLine 仍受 Mute */
        GopWrite(Text);
    }
    XhciFtdiWrite(Text);
    EhciFtdiWrite(Text);
    XhciCdcWrite(Text);
}

void HalSerialBootMark(const char *Text) {
    HalSerialBootMarkChannel(TOY_SLOG_BOOT, Text);
}
