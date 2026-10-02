/*
 * SmpBoot.c — MADT + AP 拉起 / CPU Id·ticks（PR-S3-smpboot-1）
 */
#include "AcpiMadt.h"
#include "Hal.h"
#include "Platform.h"
#include "HalPort.h"
#include "Arch.h"
#include "Scheduler.h"
#include "SmpBootPrivate.h"

UINT8 gApicIds[HAL_MAX_CPUS];
int gCpuCount = 1;
UINT8 gBspApicId;
UINT8 gApStacks[HAL_MAX_CPUS][8192] __attribute__((aligned(16)));
volatile UINT32 gApHelloCount;
volatile UINT64 gCpuTicks[HAL_MAX_CPUS];

void NormalizeBspFirst(UINT8 BspId, int Count) {
    int i;
    for (i = 0; i < Count; i++) {
        if (gApicIds[i] == BspId) {
            UINT8 Tmp = gApicIds[0];
            gApicIds[0] = gApicIds[i];
            gApicIds[i] = Tmp;
            return;
        }
    }
    if (Count < HAL_MAX_CPUS) {
        for (i = Count; i > 0; i--) {
            gApicIds[i] = gApicIds[i - 1];
        }
        gApicIds[0] = BspId;
        gCpuCount = Count + 1;
    }
}

/* AP 入口：ArchApInit + 本核 LAPIC timer，tick 空转（不进调度） */

void SmpApEntry(void) {
    SMP_BOOT_PARAM *Param = (SMP_BOOT_PARAM *)(UINTN)SMP_PARAM_PHYS;
    UINT32 Logical = Param->LogicalCpu;
    UINT8 Id = LapicGetId();

    if (Logical >= HAL_MAX_CPUS) {
        Logical = HAL_MAX_CPUS - 1;
    }

    ArchApInit(Logical);
    TimerStart();
    /* AP 验证用慢定时器，避免 QEMU 下高频 IRQ 拖死启动路径 */
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + 0x380) = 5000000u;

    (void)Id; /* 每核 hello 刷屏；汇总见 APs started */
    gApHelloCount++;
    __asm__ volatile ("" ::: "memory");
    Param->Ready = SMP_READY_MAGIC;

    __asm__ volatile ("sti" ::: "memory");
    /* 等 BSP SchedulerStart 后进入本核 idle（PR-S3） */
    while (!SchedulerIsOnline()) {
        __asm__ volatile ("pause");
    }
    /*
     * 上面 5000000 只为启动等待少挨 IRQ。周期定时器会一直重装这个初值。
     * 若留着：NUC 上 AP 约 2～1Hz，CondResched 的 hlt 要等一整拍，
     * 拖窗约 1 秒顿一次。进调度前改回与 BSP 相同的 50000。
     */
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + 0x380) = 50000u;
    /* 首入 idle 前关 IF；细节见 SchedulerApStart 注释（防 OnTimer 砸 Frame） */
    __asm__ volatile ("cli" ::: "memory");
    SchedulerApStart();
    for (;;) {
        __asm__ volatile ("hlt");
    }
}


int HalCpuCount(void) {
    return gCpuCount > 0 ? gCpuCount : 1;
}

UINT8 HalCpuApicId(UINT32 LogicalCpu) {
    if (gCpuCount <= 0) {
        return gBspApicId;
    }
    if (LogicalCpu >= (UINT32)gCpuCount || LogicalCpu >= HAL_MAX_CPUS) {
        return gApicIds[0];
    }
    return gApicIds[LogicalCpu];
}

UINT32 HalCpuGetId(void) {
    UINT8 Apic = LapicGetId();
    int i;

    if (Apic == gBspApicId) {
        return 0;
    }
    for (i = 1; i < gCpuCount && i < HAL_MAX_CPUS; i++) {
        if (gApicIds[i] == Apic) {
            return (UINT32)i;
        }
    }
    return 0;
}

int HalCpuIsBootstrapProcessor(void) {
    return LapicGetId() == gBspApicId;
}

void HalCpuIncrementTicks(void) {
    UINT32 Id = HalCpuGetId();
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

int HalSmpStartApplicationProcessors(void) {
    UINT64 Rsdp = HalPlatformRsdp();
    int Count = 0;
    UINT8 BspFromMadt = 0;
    int i;
    int Started = 0;
    UINT8 BspId;
    UINT32 Logical;

    gCpuCount = 1;
    gApHelloCount = 0;
    for (i = 0; i < HAL_MAX_CPUS; i++) {
        gCpuTicks[i] = 0;
    }
    BspId = LapicGetId();
    gBspApicId = BspId;
    gApicIds[0] = BspId;

    if (Rsdp == 0) {
        SmpLog("Smp: No RSDP (Single CPU)\n");
        return 0;
    }
    /* 电源与 MADT 解耦：MADT 失败仍应能短按关机；BootLog 以便 PHOTO 尾能抄到 */
    if (AcpiPowerInit(Rsdp) == 0) {
        HalSerialBootMark("Boot: ACPI Power Ready\n");
    } else {
        HalSerialBootMark("Boot: ACPI Power N/A\n");
    }
    if (AcpiMadtParse(Rsdp, gApicIds, HAL_MAX_CPUS, &Count, &BspFromMadt) != 0) {
        return 0;
    }
    gCpuCount = Count;
    NormalizeBspFirst(BspId, Count);
    Count = gCpuCount;

    SmpLog("Smp: MADT CPUs=");
    SmpLogHex32((UINT32)Count);
    SmpLog(" bsp_apic=");
    SmpLogHex32(BspId);
    SmpLog("\n");

    for (i = 1; i < Count; i++) {
        Logical = (UINT32)i;
        if (StartOneAp(gApicIds[i], Logical) == 0) {
            Started++;
        }
    }
    /* 只统计实际起来的核，避免调度器以为有幽灵 AP */
    gCpuCount = 1 + Started;

    SmpLog("Smp: APs Started=");
    SmpLogHex32((UINT32)Started);
    SmpLog(" hellos=");
    SmpLogHex32(gApHelloCount);
    SmpLog("\n");

    /*
     * PR-S2：Ready 已证明 AP 起来；此处只短等第一个 tick 方便串口日志。
     * 勿用超大 DelayLoops——QEMU TCG + -smp 下 BSP 易被饿死。
     */
    if (Started > 0) {
        int Wait;
        for (Wait = 0; Wait < 40; Wait++) {
            if (gCpuTicks[1] != 0) {
                break;
            }
            DelayLoops(20000);
        }
    }
    /* 逐核 ticks 过长；需要时再开 SERIAL_SMP 细查 */
    if (Started == 0 && Count > 1) {
        SmpLog("Smp: Continue Single-CPU (AP Failed)\n");
    }
    return 0;
}
