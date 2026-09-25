/*
 * UsbMsc.c — BOT 门面（PR-H-msc-2…7b）
 *
 * Init / Scan / Claim / Capacity / Mount（Mux）。
 * PR-H-msc-7b：FS 前 auto（Live 默认开；msc=0 / MSC.OFF 可关）。
 */
#include "UsbMsc.h"
#include "XHCI.h"
#include "Block.h"
#include "BlockMux.h"
#include "ToySerialLog.h"
#include "Debug.h"

void BlockMscInstall(void); /* BlockMsc.c */

#ifndef TOY_MSC_AUTO_DEFAULT
#define TOY_MSC_AUTO_DEFAULT 1 /* Live 默认开 */
#endif

static int gMscAuto = TOY_MSC_AUTO_DEFAULT;

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

int UsbMscFlush(void) {
    return XhciMscFlush();
}

int UsbMscAutoEnabled(void) {
    return gMscAuto ? 1 : 0;
}

void UsbMscAutoSet(int On) {
    gMscAuto = On ? 1 : 0;
}

/*
 * PR-H-msc-6：装 Mux，不扫描 FAT（交给 FileSystemRemountVolumes / Init）。
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

/*
 * PR-H-msc-7b：claim + Mux。失败不挡启动（返回非 0）。
 * 成功 0（已装 Mux）；无设备 / 失败 >0 或 <0 见日志。
 */
int UsbMscAutoBeforeFs(void) {
    int Claim;
    int Rc;

    if (!UsbMscAutoEnabled()) {
        return 1;
    }

    Claim = UsbMscClaim();
    if (Claim < 0) {
        ToyLogBoot("Boot: MSC Auto No HC\n");
        return -1;
    }
    if (Claim == 0) {
        return 1;
    }

    Rc = UsbMscMount();
    if (Rc != 0) {
        ToyLogBoot("Boot: MSC Auto Mux Fail\n");
        return Rc;
    }
    ToyLogBoot("Boot: MSC Auto Mux OK\n");
    return 0;
}

int UsbMscRelease(void) {
    BlockMuxRemoveMsc();
    return XhciMscRelease();
}

int UsbMscHotPoll(void) {
    if (!UsbMscReady()) {
        return 0;
    }
    if (XhciMscPresent()) {
        return 0;
    }
    DebugWrite("msc: hot unplug → release\n");
    (void)UsbMscRelease();
    return 1;
}

/*
 * 0 = 已 ready 且在位（或刚挂上）；1 = 无 MSC；负 = 失败。
 * 不在此 Remount（交给 Shell / Desktop）。
 */
int UsbMscHot(void) {
    int Claim;
    int Rc;

    if (UsbMscReady()) {
        if (XhciMscPresent()) {
            return 0;
        }
        (void)UsbMscRelease();
    }

    Claim = UsbMscClaim();
    if (Claim < 0) {
        return -1;
    }
    if (Claim == 0) {
        return 1;
    }
    Rc = UsbMscMount();
    if (Rc != 0) {
        (void)UsbMscRelease();
        return Rc < 0 ? Rc : -2;
    }
    return 0;
}
