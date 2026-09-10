/*
 * HalSerial.c — QEMU virt aarch64 PL011 UART（基址见 BoardConfig.h / PR-B2）
 *
 * TOY_SERIAL=0：不碰 UART；分模块 quiet 见 ToySerialConfig.h。
 */
#include "HalSerial.h"
#include "BoardConfig.h"
#include "ToySerialConfig.h"

#define PL011_BASE  ((UINTN)TOY_BOARD_UART_BASE)
#define PL011_DR    (*(volatile UINT32 *)(PL011_BASE + 0x00))
#define PL011_FR    (*(volatile UINT32 *)(PL011_BASE + 0x18))
#define PL011_FR_TXFF  (1u << 5)

static int ChannelUartOn(int Channel) {
#if !TOY_SERIAL
    (void)Channel;
    return 0;
#else
    switch (Channel) {
    case TOY_SLOG_BOOT: return TOY_SERIAL_BOOT;
    case TOY_SLOG_USB:  return TOY_SERIAL_USB;
    case TOY_SLOG_SMP:  return TOY_SERIAL_SMP;
    case TOY_SLOG_GUI:  return TOY_SERIAL_GUI;
    case TOY_SLOG_NET:  return TOY_SERIAL_NET;
    case TOY_SLOG_FS:   return TOY_SERIAL_FS;
    case TOY_SLOG_MEM:  return TOY_SERIAL_MEM;
    case TOY_SLOG_DRV:  return TOY_SERIAL_DRV;
    case TOY_SLOG_MISC:
    default:            return TOY_SERIAL_MISC;
    }
#endif
}

static void UartWriteRaw(const char *Text) {
#if !TOY_SERIAL
    (void)Text;
#else
    if (Text == 0) {
        return;
    }
    while (*Text) {
        while (PL011_FR & PL011_FR_TXFF) {
        }
        PL011_DR = (UINT32)(UINT8)(*Text++);
    }
#endif
}

void HalSerialInit(void) {
    /* QEMU virt 已初始化 PL011；SERIAL=0 时也不访问 */
}

int HalSerialPresent(void) {
#if TOY_SERIAL
    return 1;
#else
    return 0;
#endif
}

void HalSerialGopEnable(void) {
}

void HalSerialGopMirror(int Enable) {
    (void)Enable;
}

const char *HalSerialLogText(void) {
    return "";
}

void HalSerialWriteChannel(int Channel, const char *Text) {
    if (!ChannelUartOn(Channel)) {
        return;
    }
    UartWriteRaw(Text);
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

int HalSerialDataReady(void) {
#if !TOY_SERIAL
    return 0;
#else
    return (PL011_FR & (1u << 4)) ? 0 : 1;
#endif
}

char HalSerialReadChar(void) {
#if !TOY_SERIAL
    return 0;
#else
    while (!HalSerialDataReady()) {
    }
    return (char)(PL011_DR & 0xFFu);
#endif
}

void HalSerialBootLogRewind(void) {
}

void HalSerialGopMute(int Mute) {
    (void)Mute;
}

void HalSerialBootMarkChannel(int Channel, const char *Text) {
    HalSerialWriteChannel(Channel, Text);
}

void HalSerialBootMark(const char *Text) {
    HalSerialBootMarkChannel(TOY_SLOG_BOOT, Text);
}

void HalSerialGopPhotoHold(UINT32 Seconds) {
    (void)Seconds;
}

void HalSerialFormatHex(char *Buf, UINT64 Value, int Digits) {
    static const char Hex[] = "0123456789abcdef";
    int i;

    if (Buf == 0 || Digits <= 0 || Digits > 16) {
        return;
    }
    for (i = Digits - 1; i >= 0; i--) {
        Buf[i] = Hex[Value & 0xFu];
        Value >>= 4;
    }
    Buf[Digits] = 0;
}
