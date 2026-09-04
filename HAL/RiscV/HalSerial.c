/*
 * HalSerial.c — QEMU virt riscv64 UART16550（MMIO 0x10000000）
 */
#include "HalSerial.h"

#define UART_BASE  0x10000000u
#define UART_THR   (*(volatile UINT8 *)(UART_BASE + 0x00))
#define UART_LSR   (*(volatile UINT8 *)(UART_BASE + 0x05))
#define UART_LSR_THRE  (1u << 5)
#define UART_LSR_DR    (1u << 0)

void HalSerialInit(void) {
    /* QEMU virt 16550 已就绪 */
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

void HalSerialHexFormat(char *Buf, UINT64 Value, int Digits) {
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
