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
