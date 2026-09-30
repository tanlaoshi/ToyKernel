/*
 * Ata.c — ATA PIO 模式磁盘读写（Primary Master/Slave，经 HalIo）
 */
#include "Ata.h"
#include "Debug.h"
#include "Hal.h"

#define ATA_DATA   0x1F0
#define ATA_SECCNT 0x1F2
#define ATA_LBA0   0x1F3
#define ATA_LBA1   0x1F4
#define ATA_LBA2   0x1F5
#define ATA_HDDEV  0x1F6
#define ATA_CMD    0x1F7
#define ATA_STATUS 0x1F7

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static UINT8 DriveSelect(UINT32 Drive) {
    return (UINT8)((Drive ? 0xF0 : 0xE0));
}

static int WaitNotBusy(int Timeout) {
    while (Timeout-- > 0) {
        UINT8 St = HalIoRead8(ATA_STATUS);
        if (!(St & ATA_SR_BSY)) {
            return 1;
        }
    }
    return 0;
}

static int WaitDrq(int Timeout) {
    while (Timeout-- > 0) {
        UINT8 St = HalIoRead8(ATA_STATUS);
        if (St & ATA_SR_BSY) {
            continue;
        }
        if (St & ATA_SR_DRQ) {
            return 1;
        }
        if (St & ATA_SR_ERR) {
            return 0;
        }
    }
    return 0;
}

int AtaProbe(UINT32 Drive) {
    if (Drive > 1) {
        return 0;
    }
    if (!WaitNotBusy(100000)) {
        return 0;
    }
    HalIoWrite8(ATA_HDDEV, DriveSelect(Drive));
    {
        UINT8 St = HalIoRead8(ATA_STATUS);
        if (St == 0xFF) {
            return 0;
        }
    }
    return WaitNotBusy(100000) ? 1 : 0;
}

int AtaInit(void) {
    if (!AtaProbe(0)) {
        DebugWrite("ATA: primary master missing\n");
        return 0;
    }
    DebugWrite("ATA: primary master ready\n");
    return 1;
}

int AtaReadSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, void *Buffer) {
    UINT16 *Words = (UINT16 *)Buffer;
    UINT8 Sel = DriveSelect(Drive);

    if (Drive > 1 || Count == 0 || !Buffer) {
        return 0;
    }

    for (UINT32 S = 0; S < Count; S++) {
        UINT32 Cur = Lba + S;
        if (!WaitNotBusy(1000000)) {
            return 0;
        }
        HalIoWrite8(ATA_HDDEV, (UINT8)(Sel | ((Cur >> 24) & 0x0F)));
        HalIoWrite8(ATA_SECCNT, 1);
        HalIoWrite8(ATA_LBA0, (UINT8)(Cur & 0xFF));
        HalIoWrite8(ATA_LBA1, (UINT8)((Cur >> 8) & 0xFF));
        HalIoWrite8(ATA_LBA2, (UINT8)((Cur >> 16) & 0xFF));
        HalIoWrite8(ATA_CMD, 0x20);
        if (!WaitDrq(1000000)) {
            return 0;
        }
        for (int i = 0; i < 256; i++) {
            Words[i] = HalIoRead16(ATA_DATA);
        }
        Words += 256;
    }
    return 1;
}

int AtaWriteSectors(UINT32 Drive, UINT32 Lba, UINT32 Count, const void *Buffer) {
    const UINT16 *Words = (const UINT16 *)Buffer;
    UINT8 Sel = DriveSelect(Drive);

    if (Drive > 1 || Count == 0 || !Buffer) {
        return 0;
    }

    for (UINT32 S = 0; S < Count; S++) {
        UINT32 Cur = Lba + S;
        if (!WaitNotBusy(1000000)) {
            return 0;
        }
        HalIoWrite8(ATA_HDDEV, (UINT8)(Sel | ((Cur >> 24) & 0x0F)));
        HalIoWrite8(ATA_SECCNT, 1);
        HalIoWrite8(ATA_LBA0, (UINT8)(Cur & 0xFF));
        HalIoWrite8(ATA_LBA1, (UINT8)((Cur >> 8) & 0xFF));
        HalIoWrite8(ATA_LBA2, (UINT8)((Cur >> 16) & 0xFF));
        HalIoWrite8(ATA_CMD, 0x30);
        if (!WaitDrq(1000000)) {
            return 0;
        }
        for (int i = 0; i < 256; i++) {
            HalIoWrite16(ATA_DATA, Words[i]);
        }
        Words += 256;
        if (!WaitNotBusy(1000000)) {
            return 0;
        }
        if (HalIoRead8(ATA_STATUS) & ATA_SR_ERR) {
            return 0;
        }
    }
    return 1;
}
