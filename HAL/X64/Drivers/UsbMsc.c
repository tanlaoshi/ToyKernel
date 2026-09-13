/*
 * UsbMsc.c — BOT 门面（PR-H-msc-2…6）
 *
 * Init：Bulk 环。Scan：class 日志。Claim：SetConfig+Bulk。
 * Capacity：INQUIRY + READ CAPACITY(10)。
 * Mount：512B 校验 + BlockMuxInstallMsc（不自动挂；Shell `msc mount`）。
 */
#include "UsbMsc.h"
#include "XHCI.h"
#include "Block.h"

void BlockMscInstall(void); /* BlockMsc.c */

int UsbMscInit(void) {
    return XhciMscBringUp();
}

int UsbMscReady(void) {
    return XhciMscReady();
}

int UsbMscScan(void) {
    return XhciMscScanPorts();
}

int UsbMscClaim(void) {
    return XhciMscClaimPorts();
}

int UsbMscCapacity(void) {
    return XhciMscCapacity();
}

UINT32 UsbMscBlockCount(void) {
    return XhciMscBlockCount();
}

UINT32 UsbMscBlockSize(void) {
    return XhciMscBlockSize();
}

int UsbMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    return XhciMscReadSectors(Lba, Count, Buffer);
}

int UsbMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    return XhciMscWriteSectors(Lba, Count, Buffer);
}

/*
 * PR-H-msc-6：装 Mux，不扫描 FAT（交给 FileSystemRemountVolumes）。
 * 成功 0；未 claim -1；capacity 失败 -2；非 512B -3。
 */
int UsbMscMount(void) {
    UINT32 Bsz;

    if (!UsbMscReady()) {
        return -1;
    }
    if (UsbMscBlockCount() == 0 || UsbMscBlockSize() == 0) {
        if (UsbMscCapacity() < 0) {
            return -2;
        }
    }
    Bsz = UsbMscBlockSize();
    if (Bsz != BLOCK_SECTOR_SIZE) {
        return -3;
    }
    BlockMscInstall();
    return 0;
}
