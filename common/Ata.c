/*
 * Ata.c — ATA PIO 模式磁盘读写
 *
 * 每次读取一个 512 字节扇区，使用 LBA28 寻址。无 DMA、无多盘支持。
 */
#include "Ata.h"
#include "Console.h"

#define ATA_DATA   0x1F0
#define ATA_ERROR  0x1F1
#define ATA_SECCNT 0x1F2
#define ATA_LBA0   0x1F3
#define ATA_LBA1   0x1F4
#define ATA_LBA2   0x1F5
#define ATA_HDDEV  0x1F6
#define ATA_CMD    0x1F7
#define ATA_STATUS 0x1F7

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08

static inline void Outb(UINT16 Port, UINT8 Value) {
    __asm__ volatile ("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

static inline UINT8 Inb(UINT16 Port) {
    UINT8 Value;
    __asm__ volatile ("inb %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

static inline void Outw(UINT16 Port, UINT16 Value) {
    __asm__ volatile ("outw %0, %1" : : "a"(Value), "Nd"(Port));
}

static inline UINT16 Inw(UINT16 Port) {
    UINT16 Value;
    __asm__ volatile ("inw %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

/* 等待状态寄存器 BSY 位清零 */
static int WaitNotBusy(int Timeout) {
    while (Timeout-- > 0) {
        UINT8 St = Inb(ATA_STATUS);
        if (!(St & ATA_SR_BSY)) {
            return 1;
        }
    }
    return 0;
}

/* 等待 DRQ 置位（数据请求就绪） */
static int WaitDrq(int Timeout) {
    while (Timeout-- > 0) {
        UINT8 St = Inb(ATA_STATUS);
        if (St & ATA_SR_BSY) {
            continue;
        }
        if (St & ATA_SR_DRQ) {
            return 1;
        }
        if (St & 0x01) {
            return 0;
        }
    }
    return 0;
}

/* 检测主盘是否就绪 */
int AtaInit(void) {
    if (!WaitNotBusy(100000)) {
        ConsoleWrite("ATA: timeout\n");
        return 0;
    }
    ConsoleWrite("ATA: primary master ready\n");
    return 1;
}

/* 从 Lba 起连续读取 Count 个扇区到 Buffer（每扇区 512 字节） */
int AtaReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    UINT16 *Words = (UINT16 *)Buffer;
    if (Count == 0) {
        return 0;
    }

    for (UINT32 S = 0; S < Count; S++) {
        UINT32 Cur = Lba + S;
        if (!WaitNotBusy(1000000)) {
            return 0;
        }
        Outb(ATA_SECCNT, 1);
        Outb(ATA_LBA0, (UINT8)(Cur & 0xFF));
        Outb(ATA_LBA1, (UINT8)((Cur >> 8) & 0xFF));
        Outb(ATA_LBA2, (UINT8)((Cur >> 16) & 0xFF));
        Outb(ATA_HDDEV, (UINT8)(0xE0 | ((Cur >> 24) & 0x0F)));
        Outb(ATA_CMD, 0x20);
        if (!WaitDrq(1000000)) {
            return 0;
        }
        for (int i = 0; i < 256; i++) {
            Words[i] = Inw(ATA_DATA);
        }
        Words += 256;
    }
    return 1;
}
