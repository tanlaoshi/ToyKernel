/*
 * IwlHw.c — MMIO / nic lock / prepare / APM / start_hw（PR-N-wifi-2）
 *
 * prepare 对照 Linux iwl_pcie_set_hw_ready / prepare_card_hw（只读）。
 */
#include "IwlPrivate.h"
#include "Hal.h"
#include "PCIe.h"
#include "Scheduler.h"

volatile UINT8 *gIwlBar;
UINT64 gIwlBarPhys;
UINT64 gIwlBarSize;
UINT16 gIwlHwRev;
static int gNicLockDepth;

UINT32 IwlMmioR32(UINT32 Off) {
    UINT32 V = *(volatile UINT32 *)(gIwlBar + Off);
    __asm__ volatile ("" ::: "memory");
    return V;
}

void IwlMmioW32(UINT32 Off, UINT32 Val) {
    *(volatile UINT32 *)(gIwlBar + Off) = Val;
    __asm__ volatile ("mfence" ::: "memory");
}

void IwlMmioSet(UINT32 Off, UINT32 Mask) {
    IwlMmioW32(Off, IwlMmioR32(Off) | Mask);
}

void IwlMmioClr(UINT32 Off, UINT32 Mask) {
    IwlMmioW32(Off, IwlMmioR32(Off) & ~Mask);
}

void IwlStallUs(UINT32 Us) {
    UINT32 Lo;
    UINT32 Hi;
    UINT64 T0;
    UINT64 Need;
    UINT64 Now;

    if (Us == 0) {
        return;
    }
    __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
    T0 = ((UINT64)Hi << 32) | Lo;
    /* ~3GHz 估；宁可多转几圈 */
    Need = (UINT64)Us * 3000ULL;
    for (;;) {
        __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
        Now = ((UINT64)Hi << 32) | Lo;
        if (Now - T0 >= Need) {
            break;
        }
        HalCpuRelax();
    }
}

void IwlStallMs(UINT32 Ms) {
    UINT32 i;

    for (i = 0; i < Ms; i++) {
        IwlStallUs(1000);
        /* 刀 #117：每 ms 呼吸，WiFi 后台关联时桌面键鼠仍能动 */
        SchedulerIoBreath();
    }
}

/* 真机 FH DMA：把缓冲从 cache 推到内存 */
void IwlFlushDma(const void *Ptr, UINTN Size) {
    const UINT8 *P = (const UINT8 *)Ptr;
    UINTN Off;

    if (!Ptr || Size == 0) {
        return;
    }
    for (Off = 0; Off < Size; Off += 64) {
        __asm__ volatile("clflush (%0)" : : "r"(P + Off) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");
}

int IwlPollBit(UINT32 Off, UINT32 Bits, UINT32 Mask, UINT32 TimeoutUs) {
    UINT32 Spent = 0;

    while (Spent < TimeoutUs) {
        if ((IwlMmioR32(Off) & Mask) == Bits) {
            return 1;
        }
        IwlStallUs(10);
        Spent += 10;
    }
    return 0;
}

int IwlNicLock(void) {
    int i;

    if (gNicLockDepth > 0) {
        gNicLockDepth++;
        return 1;
    }
    IwlMmioSet(IWL_CSR_GP_CNTRL, IWL_CSR_GP_MAC_ACCESS_REQ);
    /* family-8000：Linux grab_nic_access 在置 REQ 后 udelay(2) */
    IwlStallUs(2);
    for (i = 0; i < 15000; i++) {
        UINT32 V = IwlMmioR32(IWL_CSR_GP_CNTRL);
        if ((V & (IWL_CSR_GP_MAC_CLOCK_READY | IWL_CSR_GP_GOING_TO_SLEEP))
            == IWL_CSR_GP_MAC_CLOCK_READY) {
            gNicLockDepth = 1;
            return 1;
        }
        IwlStallUs(2);
    }
    IwlMmioClr(IWL_CSR_GP_CNTRL, IWL_CSR_GP_MAC_ACCESS_REQ);
    return 0;
}

void IwlNicUnlock(void) {
    if (gNicLockDepth <= 0) {
        return;
    }
    gNicLockDepth--;
    if (gNicLockDepth > 0) {
        return;
    }
    IwlMmioClr(IWL_CSR_GP_CNTRL, IWL_CSR_GP_MAC_ACCESS_REQ);
}

UINT32 IwlPrphR(UINT32 Addr) {
    IwlMmioW32(IWL_HBUS_TARG_PRPH_RADDR, (Addr & 0x000FFFFFu) | (3u << 24));
    return IwlMmioR32(IWL_HBUS_TARG_PRPH_RDAT);
}

void IwlPrphW(UINT32 Addr, UINT32 Val) {
    IwlMmioW32(IWL_HBUS_TARG_PRPH_WADDR, (Addr & 0x000FFFFFu) | (3u << 24));
    IwlMmioW32(IWL_HBUS_TARG_PRPH_WDAT, Val);
}

/* 把 PCI PM 拉到 D0（BIOS 可能留在 D3） */
static void IwlPciForceD0(UINT8 Bus, UINT8 Dev, UINT8 Fn) {
    UINT32 Status = PciReadConfig(Bus, Dev, Fn, 0x04);
    UINT8 Cap = (UINT8)(PciReadConfig(Bus, Dev, Fn, 0x34) & 0xFCu);
    int Guard = 0;

    PciWriteConfig(Bus, Dev, Fn, 0x04, Status | 0x06u); /* Mem+BusMaster */
    while (Cap && Cap != 0xFFu && Guard++ < 48) {
        UINT32 Hdr = PciReadConfig(Bus, Dev, Fn, Cap);
        UINT8 Id = (UINT8)(Hdr & 0xFFu);
        UINT8 Next = (UINT8)((Hdr >> 8) & 0xFFu);

        if (Id == 0x01u) { /* PCI PM */
            UINT32 Pmcsr = PciReadConfig(Bus, Dev, Fn, Cap + 4);
            if ((Pmcsr & 0x3u) != 0) {
                PciWriteConfig(Bus, Dev, Fn, Cap + 4, Pmcsr & ~0x3u);
                IwlStallMs(10);
            }
            break;
        }
        Cap = Next & 0xFCu;
    }
}

/*
 * 写 NIC_READY（OWN_SET）再轮询；成功则 OS_ALIVE。
 * 对照 Linux iwl_pcie_set_hw_ready。
 */
static int IwlSetHwReady(void) {
    IwlMmioSet(IWL_CSR_HW_IF_CONFIG, IWL_CSR_HW_IF_NIC_READY);
    if (!IwlPollBit(IWL_CSR_HW_IF_CONFIG, IWL_CSR_HW_IF_NIC_READY,
                    IWL_CSR_HW_IF_NIC_READY, 50)) {
        return 0;
    }
    IwlMmioSet(IWL_CSR_MBOX_SET, IWL_CSR_MBOX_OS_ALIVE);
    return 1;
}

static int IwlPrepareCard(void) {
    int Iter;
    int T;

    if (IwlSetHwReady()) {
        return 1;
    }
    IwlMmioSet(IWL_CSR_DBG_LINK_PWR, IWL_CSR_LINK_PWR_MGMT_DIS);
    IwlStallUs(1000);

    for (Iter = 0; Iter < 10; Iter++) {
        IwlMmioSet(IWL_CSR_HW_IF_CONFIG, IWL_CSR_HW_IF_PREPARE);
        T = 0;
        while (T < 150000) {
            if (IwlSetHwReady()) {
                return 1;
            }
            IwlStallUs(200);
            T += 200;
        }
        IwlStallMs(25);
    }
    return 0;
}

static void IwlApmConfig(void) {
    /* bit2：Linux 名 L0S_DISABLED；有 L1 时置位，走 L0→L1 */
    IwlMmioSet(IWL_CSR_GIO_REG, IWL_CSR_GIO_L0S_ENABLED);
}

static int IwlApmInit(void) {
    IwlMmioSet(IWL_CSR_GIO_CHICKEN, IWL_CSR_GIO_L1A_NO_L0S_RX);
    IwlMmioSet(IWL_CSR_DBG_HPET, IWL_CSR_DBG_HPET_VAL);
    IwlMmioSet(IWL_CSR_HW_IF_CONFIG, IWL_CSR_HW_IF_HAP_WAKE_L1A);
    IwlApmConfig();
    IwlMmioSet(IWL_CSR_GP_CNTRL, IWL_CSR_GP_INIT_DONE);
    /* family-8000：activate_nic 在 INIT_DONE 后 udelay(2) 再 poll */
    IwlStallUs(2);
    if (!IwlPollBit(IWL_CSR_GP_CNTRL, IWL_CSR_GP_MAC_CLOCK_READY,
                    IWL_CSR_GP_MAC_CLOCK_READY, 25000)) {
        return 0;
    }
    return 1;
}

static void IwlPciRetryOff(UINT8 Bus, UINT8 Dev, UINT8 Fn) {
    /* Linux：PCI 0x41 RETRY_TIMEOUT=0，避免 C3 干扰 */
    UINT32 V = PciReadConfig(Bus, Dev, Fn, 0x40);
    PciWriteConfig(Bus, Dev, Fn, 0x40, V & 0xFFFF00FFu);
}

int IwlHwStart(void) {
    if (!gIwlBar) {
        return 0;
    }
    IwlPciForceD0(gIwlBus, gIwlDev, gIwlFn);
    IwlPciRetryOff(gIwlBus, gIwlDev, gIwlFn);
    if (!IwlPrepareCard()) {
        return -1; /* prep fail — 调用方区分黄字 */
    }
    /* Linux sw_reset：set_bit SW_RESET 后 usleep，再 prepare 夺回 OWN */
    IwlMmioSet(IWL_CSR_RESET, IWL_CSR_RESET_SW);
    IwlStallMs(5);
    if (!IwlPrepareCard()) {
        return -1;
    }
    if (!IwlApmInit()) {
        return -2;
    }
    gIwlHwRev = (UINT16)IwlMmioR32(IWL_CSR_HW_REV);
    /* NicInit 前探针：若此处活、fhpre 死 → NicInit/shadow 打坏 FH */
    IwlFhProbeAccess("fhapm");
    return 1;
}
