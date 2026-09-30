/*
 * UsbMsc.c — BOT 门面（PR-H-msc-2…7b + PR-H-ehci-3）
 *
 * Init / Scan / Claim / Capacity / Mount（Mux）。
 * 后端：优先 xHCI；无设备再试 EHCI（N56VZ USB2 口走 RMH）。
 */
#include "UsbMsc.h"
#include "XHCI.h"
#include "Ehci.h"
#include "Block.h"
#include "BlockMux.h"
#include "ToySerialLog.h"
#include "Debug.h"

void BlockMscInstall(void); /* BlockMsc.c */

#ifndef TOY_MSC_AUTO_DEFAULT
#define TOY_MSC_AUTO_DEFAULT 1 /* Live 默认开 */
#endif

#define MSC_BE_NONE 0
#define MSC_BE_XHCI 1
#define MSC_BE_EHCI 2

static int gMscAuto = TOY_MSC_AUTO_DEFAULT;
static int gMscBe = MSC_BE_NONE;

static int BeReady(void) {
    if (gMscBe == MSC_BE_XHCI) {
        return XhciMscReady();
    }
    if (gMscBe == MSC_BE_EHCI) {
        return EhciMscReady();
    }
    return 0;
}

int UsbMscInit(void) {
    (void)XhciMscBringUp();
    return BeReady() ? 0 : -1;
}

int UsbMscReady(void) {
    if (BeReady()) {
        return 1;
    }
    /* 未记后端时探测 */
    if (XhciMscReady()) {
        gMscBe = MSC_BE_XHCI;
        return 1;
    }
    if (EhciMscReady()) {
        gMscBe = MSC_BE_EHCI;
        return 1;
    }
    return 0;
}

int UsbMscScan(void) {
    int X = XhciMscScanPorts();
    int E = 0;

    if (EhciReady()) {
        E = EhciMscScan();
    }
    if (X < 0 && E < 0) {
        return -1;
    }
    if (X < 0) {
        X = 0;
    }
    if (E < 0) {
        E = 0;
    }
    return X + E;
}

int UsbMscClaim(void) {
    int Rc;

    if (UsbMscReady()) {
        return 1;
    }

    Rc = XhciMscClaimPorts();
    if (Rc > 0) {
        gMscBe = MSC_BE_XHCI;
        return Rc;
    }

    if (EhciReady()) {
        Rc = EhciMscClaim();
        if (Rc > 0) {
            gMscBe = MSC_BE_EHCI;
            return Rc;
        }
        if (Rc < 0 && XhciMscScanPorts() < 0) {
            return -1; /* 两边都无 HC */
        }
        return Rc; /* 0=无盘 */
    }

    return Rc; /* xHCI: 0 无盘 / -1 无 HC */
}

int UsbMscCapacity(void) {
    if (gMscBe == MSC_BE_EHCI || (!gMscBe && EhciMscReady())) {
        gMscBe = MSC_BE_EHCI;
        return EhciMscCapacity();
    }
    gMscBe = MSC_BE_XHCI;
    return XhciMscCapacity();
}

UINT32 UsbMscBlockCount(void) {
    if (gMscBe == MSC_BE_EHCI || EhciMscReady()) {
        return EhciMscBlockCount();
    }
    return XhciMscBlockCount();
}

UINT32 UsbMscBlockSize(void) {
    if (gMscBe == MSC_BE_EHCI || EhciMscReady()) {
        return EhciMscBlockSize();
    }
    return XhciMscBlockSize();
}

int UsbMscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    if (gMscBe == MSC_BE_EHCI || EhciMscReady()) {
        return EhciMscReadSectors(Lba, Count, Buffer);
    }
    return XhciMscReadSectors(Lba, Count, Buffer);
}

int UsbMscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    if (gMscBe == MSC_BE_EHCI || EhciMscReady()) {
        return EhciMscWriteSectors(Lba, Count, Buffer);
    }
    return XhciMscWriteSectors(Lba, Count, Buffer);
}

int UsbMscFlush(void) {
    if (gMscBe == MSC_BE_EHCI || EhciMscReady()) {
        return EhciMscFlush();
    }
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
    if (gMscBe == MSC_BE_EHCI) {
        (void)EhciMscRelease();
    } else {
        (void)XhciMscRelease();
    }
    gMscBe = MSC_BE_NONE;
    return 0;
}

int UsbMscHotPoll(void) {
    int Present;

    if (!UsbMscReady()) {
        return 0;
    }
    if (gMscBe == MSC_BE_EHCI) {
        Present = EhciMscPresent();
    } else {
        Present = XhciMscPresent();
    }
    if (Present) {
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
    int Present;

    if (UsbMscReady()) {
        Present = (gMscBe == MSC_BE_EHCI) ? EhciMscPresent() : XhciMscPresent();
        if (Present) {
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
