/*
 * E1000Probe.c — PCI 查找、I219 调参、MSI / 链路（PR-S3-e1000probe-1）
 *
 * MAC/NVM 见 E1000Mac.c。
 */
#include "E1000.h"
#include "E1000Private.h"
#include "PCIe.h"
#include "Debug.h"
#include "Hal.h"
#include "HalPort.h"
#include "ToySerialLog.h"

static const UINT16 gE1000Ids[] = {
    E1000_DID_82540EM, /* 82540EM — QEMU e1000 */
    0x100F,            /* 82545EM */
    E1000_DID_82574L,  /* 82574L — e1000e */
    0x10F5,            /* 82567LM */
    E1000_DID_I219_LM, /* I219-LM — NUC 现场 8086:156F */
    0
};

int PciFindE1000(UINT8 *Bus, UINT8 *Dev, UINT8 *Fn, UINT64 *BarOut,
                        UINT16 *DidOut) {
    int B;
    int D;
    int F;
    int I;

    for (B = 0; B < 256; B++) {
        for (D = 0; D < 32; D++) {
            for (F = 0; F < 8; F++) {
                UINT32 VidDid = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x00);
                UINT16 Vid = (UINT16)(VidDid & 0xFFFF);
                UINT16 Did = (UINT16)(VidDid >> 16);
                UINT32 Lo;
                UINT32 Hi;
                UINT64 Bar;
                UINT32 Cmd;

                if (Vid != E1000_VENDOR) {
                    continue;
                }
                for (I = 0; gE1000Ids[I] != 0; I++) {
                    if (Did == gE1000Ids[I]) {
                        break;
                    }
                }
                if (gE1000Ids[I] == 0) {
                    continue;
                }

                Cmd = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04);
                PciWriteConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x04, Cmd | 0x06);

                Lo = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x10);
                if (Lo & 1u) {
                    continue;
                }
                Bar = Lo & 0xFFFFFFF0ULL;
                if (((Lo >> 1) & 3u) == 2u) {
                    Hi = PciReadConfig((UINT8)B, (UINT8)D, (UINT8)F, 0x14);
                    Bar |= ((UINT64)Hi) << 32;
                }
                if (Bar == 0) {
                    continue;
                }
                *Bus = (UINT8)B;
                *Dev = (UINT8)D;
                *Fn = (UINT8)F;
                *BarOut = Bar;
                if (DidOut) {
                    *DidOut = Did;
                }
                return 1;
            }
        }
    }
    return 0;
}

/*
 * PR-N-i219-tx：I219 写 TXDCTL。NUC 2026-09-22：仍 TDT↑ TDH=0 / last_rc=-3 → 假说失败，保留无害。
 */
void E1000ApplyI219TxDctl(void) {
    if (gPciDid != E1000_DID_I219_LM) {
        return;
    }
    MmioW32(E1000_REG_TXDCTL,
            (0x1fu << 0) | (1u << 8) | (1u << 16) |
            E1000_TXDCTL_GRAN | E1000_TXDCTL_QUEUE_ENABLE);
}

/*
 * PR-N-i219-tx2 曾写 TARC0；tx3 flush 在无 TARC 时 DD 成功，正式 Send 在
 * 写入 TARC 后仍 -3 → tx4 停用（保留函数供对照，Setup 不再调用）。
 */
void E1000ApplyI219Tarc(void) {
    (void)0;
}

/* PR-H4e-3：PCI MSI → VEC_E1000；失败则保持 poll（IMC 全掩） */
static void FillPciBars(USB_CONTROLLER *Dev) {
    int i;

    for (i = 0; i < 6;) {
        UINT32 Lo = PciReadConfig(Dev->Bus, Dev->Device, Dev->Function,
                                  (UINT8)(0x10 + i * 4));
        UINT64 Bar;
        UINT32 Type;

        if (Lo & 1u) {
            Dev->Bar[i] = Lo & ~0x3u;
            i++;
            continue;
        }
        Bar = Lo & 0xFFFFFFF0ULL;
        Type = (Lo >> 1) & 3u;
        if (Type == 2u && i + 1 < 6) {
            UINT32 Hi = PciReadConfig(Dev->Bus, Dev->Device, Dev->Function,
                                      (UINT8)(0x10 + (i + 1) * 4));
            Bar |= ((UINT64)Hi) << 32;
            Dev->Bar[i] = Bar;
            Dev->Bar[i + 1] = 0;
            i += 2;
        } else {
            Dev->Bar[i] = Bar;
            i++;
        }
    }
}

int TryEnableMsiRx(void) {
    USB_CONTROLLER Dev;

    ZeroMemory(&Dev, sizeof(Dev));
    Dev.Bus = gPciBus;
    Dev.Device = gPciDev;
    Dev.Function = gPciFn;
    FillPciBars(&Dev);
    if (Dev.Bar[0] == 0) {
        Dev.Bar[0] = gBarPhys;
    }

    MmioW32(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)MmioR32(E1000_REG_ICR);

    if (!PciEnableMsi(&Dev, VEC_E1000, 0)) {
        DebugWrite("e1000: MSI failed; stay poll\n");
        return 0;
    }

    /* 立刻投递；RXT0 / RXDMT0 / LSC */
    MmioW32(E1000_REG_ITR, 0);
    MmioW32(E1000_REG_IMS, E1000_IMS_RX);
    gE1000UseIrq = 1;
    return 1;
}

/* 等 STATUS.LU；超时返回 0（Setup soft-fail，不挡桌面）
 * 刀 #115：~200ms（rdtsc 粗校）封顶；旧 2e6 次 MMIO 空转无链路时可拖数秒。
 */
int WaitLinkUp(void) {
    UINT32 Lo;
    UINT32 Hi;
    UINT64 T0;
    UINT64 Limit;
    UINT64 Now;

    __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
    T0 = ((UINT64)Hi << 32) | Lo;
    Limit = T0 + 200ULL * 3000000ULL; /* 与 RtlStallMs 同粗校 */
    for (;;) {
        if (MmioR32(E1000_REG_STATUS) & E1000_STATUS_LU) {
            return 1;
        }
        __asm__ volatile("rdtsc" : "=a"(Lo), "=d"(Hi));
        Now = ((UINT64)Hi << 32) | Lo;
        if (Now >= Limit) {
            return 0;
        }
        HalCpuRelax();
    }
}
