/*
 * HalSerial.c — QEMU virt aarch64 PL011 UART（基址见 BoardConfig.h / PR-B2）
 */
#include "HalSerial.h"
#include "BoardConfig.h"

#define PL011_BASE  ((UINTN)TOY_BOARD_UART_BASE)
#define PL011_DR    (*(volatile UINT32 *)(PL011_BASE + 0x00))
#define PL011_FR    (*(volatile UINT32 *)(PL011_BASE + 0x18))
#define PL011_FR_TXFF  (1u << 5)

void HalSerialInit(void) {
    /* QEMU virt 已初始化 PL011；bringup 无需再配波特率 */
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
        while (PL011_FR & PL011_FR_TXFF) {
        }
        PL011_DR = (UINT32)(UINT8)(*Text++);
    }
}

int HalSerialDataReady(void) {
    /* RXFE=4：空则无数据 */
    return (PL011_FR & (1u << 4)) ? 0 : 1;
}

char HalSerialReadChar(void) {
    while (!HalSerialDataReady()) {
    }
    return (char)(PL011_DR & 0xFFu);
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

void HalSerialGopPhotoHold(UINT32 Seconds) {
    (void)Seconds;
}
