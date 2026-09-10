/*
 * HalSerial.c — RISC-V UART16550 / DW-APB（基址与间距见 BoardConfig.h）
 *
 * TOY_SERIAL=0：不碰 UART；分模块 quiet 见 ToySerialConfig.h。
 */
#include "HalSerial.h"
#include "BoardConfig.h"
#include "ToySerialConfig.h"

#ifndef TOY_BOARD_UART_REG_SHIFT
#define TOY_BOARD_UART_REG_SHIFT 0
#endif

#define UART_BASE  ((UINTN)TOY_BOARD_UART_BASE)
#define UART_OFF(N) ((UINTN)(N) << (TOY_BOARD_UART_REG_SHIFT))
#define UART_THR   (*(volatile UINT8 *)(UART_BASE + UART_OFF(0)))
#define UART_LSR   (*(volatile UINT8 *)(UART_BASE + UART_OFF(5)))
#define UART_LSR_THRE  (1u << 5)
#define UART_LSR_DR    (1u << 0)

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
        while ((UART_LSR & UART_LSR_THRE) == 0) {
        }
        UART_THR = (UINT8)(*Text++);
    }
#endif
}

void HalSerialInit(void) {
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
    return (UART_LSR & UART_LSR_DR) ? 1 : 0;
#endif
}

char HalSerialReadChar(void) {
#if !TOY_SERIAL
    return 0;
#else
    while (!HalSerialDataReady()) {
    }
    return (char)UART_THR;
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
