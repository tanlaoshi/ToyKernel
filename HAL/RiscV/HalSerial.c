/*
 * HalSerial.c — RISC-V UART16550 / DW-APB（基址与间距见 BoardConfig.h）
 *
 * QEMU virt：字节间距（REG_SHIFT=0）@ 0x10000000
 * Duo S：   reg-shift=2（×4）@ 0x04140000（PR-B3）
 */
#include "HalSerial.h"
#include "BoardConfig.h"

#ifndef TOY_BOARD_UART_REG_SHIFT
#define TOY_BOARD_UART_REG_SHIFT 0
#endif

#define UART_BASE  ((UINTN)TOY_BOARD_UART_BASE)
#define UART_OFF(N) ((UINTN)(N) << (TOY_BOARD_UART_REG_SHIFT))
#define UART_THR   (*(volatile UINT8 *)(UART_BASE + UART_OFF(0)))
#define UART_LSR   (*(volatile UINT8 *)(UART_BASE + UART_OFF(5)))
#define UART_LSR_THRE  (1u << 5)
#define UART_LSR_DR    (1u << 0)

void HalSerialInit(void) {
    /* 厂商 U-Boot / QEMU 已配好波特率；bringup 不重配 */
}

int HalSerialPresent(void) {
    return 1;
}

void HalSerialGopEnable(void) {
    /* x86 PR-H3 only */
}

const char *HalSerialLogText(void) {
    return "";
}

void HalSerialWrite(const char *Text) {
    if (Text == 0) {
        return;
    }
    while (*Text) {
        while ((UART_LSR & UART_LSR_THRE) == 0) {
        }
        UART_THR = (UINT8)(*Text++);
    }
}

int HalSerialDataReady(void) {
    return (UART_LSR & UART_LSR_DR) ? 1 : 0;
}

char HalSerialReadChar(void) {
    while (!HalSerialDataReady()) {
    }
    return (char)UART_THR;
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

void HalSerialBootLogRewind(void) {
}

void HalSerialGopMute(int Mute) {
    (void)Mute;
}

void HalSerialBootMark(const char *Text) {
    HalSerialWrite(Text);
}
