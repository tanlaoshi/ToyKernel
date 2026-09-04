/*
 * Smp.c — PR-A14：QEMU virt riscv64 SBI HSM + 每核 idle
 *
 * OpenSBI Boot HART 常非 0（见 gBspHartId）。逻辑 CPU0=BSP；其余 hart 经 HSM
 * 或 KernelEntry SecondaryPark soft-release 拉起。
 */
#include "Hal.h"
#include "Dtb.h"
#include "Scheduler.h"

#define SmpLog(Text)   HalDebugWrite(Text)
#define SmpLogHex32(V) HalDebugHex32(V)
#define SmpLogHex64(V) HalDebugHex64(V)

#define SBI_EXT_HSM            0x48534DULL
#define SBI_EXT_HSM_HART_START 0
#define SMP_READY_MAGIC        0x534D5052u /* 'SMPR' */
#define SMP_GO_MAGIC           0x41313447u /* 'A14G' */

extern void HalApEntry(void);
extern void HalTimerStartAp(void);
extern void HalPagingEnableAp(void);
extern void HalTrapVectorInstall(void);

extern UINT64 gBspHartId;

UINT64 gApStackTop[HAL_MAX_CPUS];
/* Startup.S SecondaryPark：按物理 hartid 索引 */
volatile UINT32 gApGo[HAL_MAX_CPUS];
volatile UINT32 gApLogical[HAL_MAX_CPUS];

static UINT8 gApStacks[HAL_MAX_CPUS][8192] __attribute__((aligned(16)));
static int gCpuCount = 1;
static volatile UINT32 gApReady[HAL_MAX_CPUS];
static volatile UINT32 gApHelloCount;
static volatile UINT64 gCpuTicks[HAL_MAX_CPUS];
static UINT64 gSmpDtbPhys;

void HalSmpNoteDtb(UINT64 DtbPhys) {
    if (DtbPhys != 0) {
        gSmpDtbPhys = DtbPhys;
    }
}

static void SetCpuId(UINT32 Id) {
    __asm__ volatile("mv tp, %0" ::"r"((UINT64)Id) : "memory");
}

static INT64 SbiHartStart(UINT64 HartId, UINT64 StartAddr, UINT64 Opaque) {
    register UINT64 A0 __asm__("a0") = HartId;
    register UINT64 A1 __asm__("a1") = StartAddr;
    register UINT64 A2 __asm__("a2") = Opaque;
    register UINT64 A6 __asm__("a6") = SBI_EXT_HSM_HART_START;
    register UINT64 A7 __asm__("a7") = SBI_EXT_HSM;
    __asm__ volatile("ecall"
                     : "+r"(A0)
                     : "r"(A1), "r"(A2), "r"(A6), "r"(A7)
                     : "memory");
    return (INT64)A0;
}

static void DelayLoops(volatile UINT32 N) {
    while (N--) {
        __asm__ volatile("" ::: "memory");
    }
}

void HalApMain(UINT32 Logical) {
    if (Logical >= HAL_MAX_CPUS) {
        Logical = HAL_MAX_CPUS - 1;
    }
    SetCpuId(Logical);
    HalPagingEnableAp();
    HalTrapVectorInstall();
    HalTimerStartAp();

    SmpLog("smp: hello cpu=");
    SmpLogHex32(Logical);
    SmpLog("\n");
    gApHelloCount++;
    __asm__ volatile("" ::: "memory");
    gApReady[Logical] = SMP_READY_MAGIC;

    while (!SchedulerIsOnline()) {
        HalCpuHalt();
    }
    SchedulerApStart();
    for (;;) {
        HalCpuPark();
    }
}

int HalCpuCount(void) {
    return gCpuCount > 0 ? gCpuCount : 1;
}

UINT32 HalCpuId(void) {
    UINT64 V;
    __asm__ volatile("mv %0, tp" : "=r"(V));
    return (UINT32)V;
}

int HalCpuIsBsp(void) {
    return HalCpuId() == 0;
}

void HalCpuTickInc(void) {
    UINT32 Id = HalCpuId();
    if (Id < HAL_MAX_CPUS) {
        gCpuTicks[Id]++;
    }
}

UINT64 HalCpuTicks(UINT32 Cpu) {
    if (Cpu >= HAL_MAX_CPUS) {
        return 0;
    }
    return gCpuTicks[Cpu];
}

int HalSmpStartAps(void) {
    int Want;
    int Hart;
    int Logical;
    int Started = 0;
    UINT64 BspHart = gBspHartId;

    SetCpuId(0);
    gCpuCount = 1;
    gApHelloCount = 0;
    for (Hart = 0; Hart < HAL_MAX_CPUS; Hart++) {
        gCpuTicks[Hart] = 0;
        gApReady[Hart] = 0;
        gApGo[Hart] = 0;
        gApLogical[Hart] = 0;
        gApStackTop[Hart] =
            (UINT64)(UINTN)(gApStacks[Hart] + sizeof(gApStacks[Hart]));
    }

    Want = DtbCpuCount(gSmpDtbPhys);
    if (Want > HAL_MAX_CPUS) {
        Want = HAL_MAX_CPUS;
    }
    if (Want < 1) {
        Want = 1;
    }

    SmpLog("smp: DTB cpus=");
    SmpLogHex32((UINT32)Want);
    SmpLog(" bsp_hart=");
    SmpLogHex32((UINT32)BspHart);
    SmpLog("\n");

    if (Want <= 1) {
        SmpLog("smp: single CPU\n");
        return 0;
    }

    Logical = 1;
    for (Hart = 0; Hart < Want; Hart++) {
        INT64 Rc;
        int Tries;
        int Soft = 0;
        UINT32 LogId;

        if ((UINT64)Hart == BspHart) {
            continue;
        }
        if (Logical >= HAL_MAX_CPUS) {
            break;
        }
        LogId = (UINT32)Logical;

        gApReady[LogId] = 0;
        gApLogical[Hart] = LogId;
        gApGo[Hart] = 0;
        /* 先试 HSM；若 hart 已在 payload（ALREADY_*）则 soft-release */
        Rc = SbiHartStart((UINT64)Hart, (UINT64)(UINTN)HalApEntry, (UINT64)LogId);
        if (Rc != 0) {
            Soft = 1;
            __asm__ volatile("" ::: "memory");
            gApGo[Hart] = SMP_GO_MAGIC;
        }
        Tries = 2000000;
        while (gApReady[LogId] != SMP_READY_MAGIC && Tries-- > 0) {
            HalCpuRelax();
        }
        if (gApReady[LogId] != SMP_READY_MAGIC) {
            SmpLog("smp: AP timeout hart=");
            SmpLogHex32((UINT32)Hart);
            SmpLog(" logical=");
            SmpLogHex32(LogId);
            if (Soft) {
                SmpLog(" (soft)");
            } else {
                SmpLog(" hsm_rc=");
                SmpLogHex64((UINT64)Rc);
            }
            SmpLog("\n");
            gApGo[Hart] = 0;
            Logical++;
            continue;
        }
        if (Soft) {
            SmpLog("smp: soft-release hart=");
            SmpLogHex32((UINT32)Hart);
            SmpLog(" logical=");
            SmpLogHex32(LogId);
            SmpLog("\n");
        }
        Started++;
        Logical++;
    }

    gCpuCount = 1 + Started;
    SmpLog("smp: APs started=");
    SmpLogHex32((UINT32)Started);
    SmpLog(" hellos=");
    SmpLogHex32(gApHelloCount);
    SmpLog("\n");

    if (Started > 0) {
        int Wait;
        for (Wait = 0; Wait < 40; Wait++) {
            if (gCpuTicks[1] != 0) {
                break;
            }
            DelayLoops(20000);
        }
    }
    SmpLog("smp: ticks");
    for (Hart = 0; Hart < gCpuCount && Hart < HAL_MAX_CPUS; Hart++) {
        SmpLog(" cpu");
        SmpLogHex32((UINT32)Hart);
        SmpLog("=");
        SmpLogHex64(gCpuTicks[Hart]);
    }
    SmpLog("\n");
    if (Started == 0 && Want > 1) {
        SmpLog("smp: continue single-CPU (AP failed)\n");
    }
    return 0;
}
