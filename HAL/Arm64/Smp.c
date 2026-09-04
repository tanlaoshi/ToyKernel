/*
 * Smp.c — PR-A14：QEMU virt aarch64 PSCI CPU_ON + 每核 idle
 */
#include "Hal.h"
#include "Dtb.h"
#include "Scheduler.h"

#define SmpLog(Text)   HalDebugWrite(Text)
#define SmpLogHex32(V) HalDebugHex32(V)
#define SmpLogHex64(V) HalDebugHex64(V)

#define PSCI_CPU_ON_64     0xC4000003ULL
#define SMP_READY_MAGIC    0x534D5052u /* 'SMPR' */
#define ARM64_VIRT_DTB_ADDR 0x4a000000ULL

extern void HalApEntry(void);
extern void HalTimerStartAp(void);
extern void HalPagingEnableAp(void);
extern void HalExceptionVectorsInstall(void);

UINT64 gApStackTop[HAL_MAX_CPUS];

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
    __asm__ volatile("msr tpidr_el1, %0" ::"r"((UINT64)Id) : "memory");
}

static INT64 PsciCpuOn(UINT64 TargetMpidr, UINT64 Entry, UINT64 Context) {
    register UINT64 X0 __asm__("x0") = PSCI_CPU_ON_64;
    register UINT64 X1 __asm__("x1") = TargetMpidr;
    register UINT64 X2 __asm__("x2") = Entry;
    register UINT64 X3 __asm__("x3") = Context;
    __asm__ volatile("hvc #0"
                     : "+r"(X0)
                     : "r"(X1), "r"(X2), "r"(X3)
                     : "memory", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
                       "x11", "x12", "x13", "x14", "x15", "x16", "x17");
    return (INT64)X0;
}

static void DelayLoops(volatile UINT32 N) {
    while (N--) {
        __asm__ volatile("yield");
    }
}

void HalApMain(UINT32 Logical) {
    if (Logical >= HAL_MAX_CPUS) {
        Logical = HAL_MAX_CPUS - 1;
    }
    SetCpuId(Logical);
    HalPagingEnableAp();
    HalExceptionVectorsInstall();
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
    __asm__ volatile("mrs %0, tpidr_el1" : "=r"(V));
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
    UINT64 Dtb;
    int Want;
    int i;
    int Started = 0;

    SetCpuId(0);
    gCpuCount = 1;
    gApHelloCount = 0;
    for (i = 0; i < HAL_MAX_CPUS; i++) {
        gCpuTicks[i] = 0;
        gApReady[i] = 0;
        gApStackTop[i] =
            (UINT64)(UINTN)(gApStacks[i] + sizeof(gApStacks[i]));
    }

    Dtb = gSmpDtbPhys ? gSmpDtbPhys : ARM64_VIRT_DTB_ADDR;
    Want = DtbCpuCount(Dtb);
    if (Want > HAL_MAX_CPUS) {
        Want = HAL_MAX_CPUS;
    }
    if (Want < 1) {
        Want = 1;
    }

    SmpLog("smp: DTB cpus=");
    SmpLogHex32((UINT32)Want);
    SmpLog("\n");

    if (Want <= 1) {
        SmpLog("smp: single CPU\n");
        return 0;
    }

    for (i = 1; i < Want; i++) {
        INT64 Rc;
        int Tries;

        gApReady[i] = 0;
        Rc = PsciCpuOn((UINT64)i, (UINT64)(UINTN)HalApEntry, (UINT64)i);
        if (Rc != 0) {
            SmpLog("smp: PSCI CPU_ON fail cpu=");
            SmpLogHex32((UINT32)i);
            SmpLog(" rc=");
            SmpLogHex64((UINT64)Rc);
            SmpLog("\n");
            continue;
        }
        Tries = 2000000;
        while (gApReady[i] != SMP_READY_MAGIC && Tries-- > 0) {
            HalCpuRelax();
        }
        if (gApReady[i] != SMP_READY_MAGIC) {
            SmpLog("smp: AP timeout cpu=");
            SmpLogHex32((UINT32)i);
            SmpLog("\n");
            continue;
        }
        Started++;
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
    for (i = 0; i < gCpuCount && i < HAL_MAX_CPUS; i++) {
        SmpLog(" cpu");
        SmpLogHex32((UINT32)i);
        SmpLog("=");
        SmpLogHex64(gCpuTicks[i]);
    }
    SmpLog("\n");
    if (Started == 0 && Want > 1) {
        SmpLog("smp: continue single-CPU (AP failed)\n");
    }
    return 0;
}
